// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <FileNameDisambiguator.h>
#include <ImageFileInfo.h>
#include <ImageInfo.h>
#include <OutputFileNameGenerator.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace {
QDomDocument readDocument(const QString& filePath) {
  QDomDocument doc;
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    doc.setContent(&file);
  }
  return doc;
}

QDomDocument parse(const QString& xml) {
  QDomDocument doc;
  doc.setContent(xml);
  return doc;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectFileTestSuite)

BOOST_AUTO_TEST_CASE(test_empty_project_keeps_input_dir) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QDir root(tempDir.path());
  BOOST_REQUIRE(root.mkdir("Buch"));
  const QString inputDir = root.absoluteFilePath("Buch");
  const QString projectFile = QDir(inputDir).absoluteFilePath("Buch.ScanTailor");

  auto pages = std::make_shared<ProjectPages>();
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(),
                                       QDir(inputDir).absoluteFilePath("out"), Qt::LeftToRight);
  const ProjectWriter writer(pages, SelectedPage(), outGen, inputDir);
  BOOST_REQUIRE(writer.write(projectFile, {}, ProcessingOptions()));

  const ProjectReader reader(readDocument(projectFile), projectFile);
  BOOST_REQUIRE(reader.success());
  BOOST_CHECK_EQUAL(reader.pages()->numImages(), 0);
  BOOST_CHECK(reader.outputDirectory() == QDir(inputDir).absoluteFilePath("out"));
  BOOST_REQUIRE_EQUAL(reader.directories().size(), 1u);
  BOOST_CHECK(QDir(reader.directories().front()) == QDir(inputDir));
  // The folder of the project file is stored as "."; the path must not end in "/." then,
  // because image paths are compared as text (e.g. after the Fix DPI dialog).
  BOOST_CHECK(reader.directories().front() == inputDir);
}

BOOST_AUTO_TEST_CASE(test_processing_options_are_stored_in_the_project) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString projectFile = QDir(tempDir.path()).absoluteFilePath("Buch.ScanTailor");

  ProcessingOptions options;
  options.tiffCompression = {COMPRESSION_LZW, COMPRESSION_JPEG, 70, COMPRESSION_DEFLATE};
  options.contentDetection = select_content::DetectionOptions(80, 20, true);
  options.colorDetection.minColorShare = 0.07;
  options.colorDetection.usePictureZones = true;
  options.colorDetection.minPictureShare = 0.12;
  options.blackOnWhiteDetection = false;
  options.blackOnWhiteAtOutput = false;
  options.pdf.colorCompression = PdfCompression::JPEG2000;
  options.pdf.colorQuality = 60;
  options.pdf.backgroundScale = 3;
  options.pdf.paletteCompression = PdfCompression::NONE;
  options.pdf.paletteQuality = 50;
  options.pdf.bitonalCompression = PdfCompression::CCITT_G4;
  options.pdf.ocr = false;
  options.pdf.ocrLanguages = QStringList{"fra", "lat"};
  options.pdf.ocrPageLayout = 2;

  auto pages = std::make_shared<ProjectPages>();
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), tempDir.path(), Qt::LeftToRight);
  BOOST_REQUIRE(ProjectWriter(pages, SelectedPage(), outGen, tempDir.path()).write(projectFile, {}, options));

  const ProjectReader reader(readDocument(projectFile), projectFile);
  BOOST_REQUIRE(reader.success());
  // The fallback must not show through.
  const ProcessingOptions read = reader.readProcessingOptions(ProcessingOptions());
  BOOST_CHECK_EQUAL(read.tiffCompression.bw, COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(read.tiffCompression.color, COMPRESSION_JPEG);
  BOOST_CHECK_EQUAL(read.tiffCompression.jpegQuality, 70);
  BOOST_CHECK_EQUAL(read.tiffCompression.palette, COMPRESSION_DEFLATE);
  BOOST_CHECK(read.contentDetection == options.contentDetection);
  BOOST_CHECK(read.colorDetection == options.colorDetection);
  BOOST_CHECK(!read.blackOnWhiteDetection);
  BOOST_CHECK(!read.blackOnWhiteAtOutput);
  BOOST_CHECK(read.pdf.colorCompression == PdfCompression::JPEG2000);
  BOOST_CHECK_EQUAL(read.pdf.colorQuality, 60);
  BOOST_CHECK_EQUAL(read.pdf.backgroundScale, 3);
  BOOST_CHECK(read.pdf.paletteCompression == PdfCompression::NONE);
  BOOST_CHECK_EQUAL(read.pdf.paletteQuality, 50);
  BOOST_CHECK(read.pdf.bitonalCompression == PdfCompression::CCITT_G4);
  BOOST_CHECK(!read.pdf.ocr);
  BOOST_CHECK(read.pdf.ocrLanguages == (QStringList{"fra", "lat"}));
  BOOST_CHECK_EQUAL(read.pdf.ocrPageLayout, 2);
}

BOOST_AUTO_TEST_CASE(test_projects_without_processing_options_take_the_fallback) {
  // As a project of ScanTailor Advanced or from before these settings were stored.
  const QDomDocument doc = parse(QStringLiteral("<project version=\"10\"/>"));
  const ProjectReader reader(doc);

  ProcessingOptions fallback;
  fallback.tiffCompression.color = COMPRESSION_JPEG;
  fallback.contentDetection = select_content::DetectionOptions(90, 50, true);
  fallback.blackOnWhiteAtOutput = false;
  fallback.pdf.ocrLanguages = QStringList{"ita"};
  const ProcessingOptions read = reader.readProcessingOptions(fallback);
  BOOST_CHECK_EQUAL(read.tiffCompression.color, COMPRESSION_JPEG);
  BOOST_CHECK(read.contentDetection == fallback.contentDetection);
  BOOST_CHECK(!read.blackOnWhiteAtOutput);
  BOOST_CHECK(read.pdf.ocrLanguages == QStringList{"ita"});
}

BOOST_AUTO_TEST_CASE(test_metadata_update_keeps_pages_of_multi_page_files) {
  const QString multiPage = QDir(QDir::tempPath()).absoluteFilePath("multi.tif");
  const QString singlePage = QDir(QDir::tempPath()).absoluteFilePath("single.png");
  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  // Pages of a multi-page file count from 1, a single-page file has 0.
  ProjectPages pages({ImageInfo(ImageId(multiPage, 1), metadata, 1, false, false),
                      ImageInfo(ImageId(multiPage, 2), metadata, 1, false, false),
                      ImageInfo(ImageId(multiPage, 3), metadata, 1, false, false),
                      ImageInfo(ImageId(singlePage, 0), metadata, 1, false, false)},
                     Qt::LeftToRight);

  // As the Fix DPI dialog does: change the second page of the multi-page file.
  std::vector<ImageFileInfo> files = pages.toImageFileInfo();
  BOOST_REQUIRE_EQUAL(files.size(), 2u);
  BOOST_REQUIRE_EQUAL(files[0].imageInfo().size(), 3u);
  files[0].imageInfo()[1] = ImageMetadata(QSize(2000, 3000), Dpi(600, 600));
  pages.updateMetadataFrom(files);

  for (const PageInfo& page : pages.toPageSequence(IMAGE_VIEW)) {
    const bool changed = (page.imageId() == ImageId(multiPage, 2));
    BOOST_CHECK_EQUAL(page.metadata().dpi().horizontal(), changed ? 600 : 300);
  }
}

BOOST_AUTO_TEST_CASE(test_empty_project_without_input_dir) {
  const ProjectReader reader(
      parse("<project version=\"4\" outputDirectory=\"/out\"><directories/><files/><images/><pages/></project>"));
  BOOST_REQUIRE(reader.success());
  BOOST_CHECK_EQUAL(reader.pages()->numImages(), 0);
  BOOST_CHECK(reader.directories().empty());
}

BOOST_AUTO_TEST_CASE(test_unreadable_images_are_an_error) {
  // The image refers to a file that is not listed, so it can't be read.
  const ProjectReader reader(
      parse("<project version=\"4\" outputDirectory=\"/out\"><directories/><files/>"
            "<images><image id=\"3\" subPages=\"1\" fileId=\"2\" fileImage=\"0\"/></images><pages/></project>"));
  BOOST_CHECK(!reader.success());
}

BOOST_AUTO_TEST_SUITE_END()
