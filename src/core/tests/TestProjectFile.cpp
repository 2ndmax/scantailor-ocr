// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <FileNameDisambiguator.h>
#include <ImageFileInfo.h>
#include <ImageInfo.h>
#include <OrthogonalRotation.h>
#include <OutputFileNameGenerator.h>
#include <PageInfo.h>
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
#include <utility>
#include <vector>

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
  BOOST_REQUIRE(writer.write(projectFile, {}, ProcessingOptions(), DefaultParams()));

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
  options.pdf.pdfA = false;
  options.pdf.ocr = false;
  options.pdf.ocrLanguages = QStringList{"fra", "lat"};
  options.pdf.ocrPageLayout = 2;

  auto pages = std::make_shared<ProjectPages>();
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), tempDir.path(), Qt::LeftToRight);
  BOOST_REQUIRE(
      ProjectWriter(pages, SelectedPage(), outGen, tempDir.path()).write(projectFile, {}, options, DefaultParams()));

  const ProjectReader reader(readDocument(projectFile), projectFile);
  BOOST_REQUIRE(reader.success());
  // The fallback must not show through.
  const ProcessingOptions read = reader.readProcessingOptions(ProcessingOptions());
  BOOST_CHECK_EQUAL(read.tiffCompression.bw, COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(read.tiffCompression.color, COMPRESSION_JPEG);
  BOOST_CHECK_EQUAL(read.tiffCompression.jpegQuality, 70);
  BOOST_CHECK_EQUAL(read.tiffCompression.palette, COMPRESSION_DEFLATE);
  BOOST_CHECK(read.colorDetection == options.colorDetection);
  BOOST_CHECK(!read.blackOnWhiteDetection);
  BOOST_CHECK(!read.blackOnWhiteAtOutput);
  BOOST_CHECK(read.pdf.colorCompression == PdfCompression::JPEG2000);
  BOOST_CHECK_EQUAL(read.pdf.colorQuality, 60);
  BOOST_CHECK_EQUAL(read.pdf.backgroundScale, 3);
  BOOST_CHECK(read.pdf.paletteCompression == PdfCompression::NONE);
  BOOST_CHECK_EQUAL(read.pdf.paletteQuality, 50);
  BOOST_CHECK(read.pdf.bitonalCompression == PdfCompression::CCITT_G4);
  BOOST_CHECK(!read.pdf.pdfA);
  BOOST_CHECK(!read.pdf.ocr);
  BOOST_CHECK(read.pdf.ocrLanguages == (QStringList{"fra", "lat"}));
  BOOST_CHECK_EQUAL(read.pdf.ocrPageLayout, 2);
}

BOOST_AUTO_TEST_CASE(test_start_values_are_stored_in_the_project) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QString projectFile = QDir(tempDir.path()).absoluteFilePath("Buch.ScanTailor");

  DefaultParams startValues;
  DefaultParams::DeskewParams deskewParams;
  deskewParams.setAutoOblique(true);
  startValues.setDeskewParams(deskewParams);
  DefaultParams::PageSplitParams pageSplitParams;
  pageSplitParams.setPageOrder(Qt::RightToLeft);
  startValues.setPageSplitParams(pageSplitParams);
  DefaultParams::SelectContentParams selectContentParams;
  selectContentParams.setDetectionOptions(select_content::DetectionOptions(80, 20, true));
  startValues.setSelectContentParams(selectContentParams);

  auto pages = std::make_shared<ProjectPages>();
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), tempDir.path(), Qt::LeftToRight);
  BOOST_REQUIRE(ProjectWriter(pages, SelectedPage(), outGen, tempDir.path())
                    .write(projectFile, {}, ProcessingOptions(), startValues));

  const QDomDocument doc = readDocument(projectFile);
  // The processing options are stored once, for the project.
  BOOST_CHECK(doc.documentElement().namedItem("default-params").namedItem("processing-options").isNull());
  const ProjectReader reader(doc, projectFile);
  const std::unique_ptr<DefaultParams> read = reader.readDefaultParams();
  BOOST_REQUIRE(read);
  BOOST_CHECK(read->getDeskewParams().isAutoOblique());
  BOOST_CHECK(read->getPageSplitParams().getPageOrder() == Qt::RightToLeft);
  BOOST_CHECK(read->getSelectContentParams().getDetectionOptions() == select_content::DetectionOptions(80, 20, true));

  // Projects from before have none; they take the active profile.
  BOOST_CHECK(!ProjectReader(parse(QStringLiteral("<project version=\"10\"/>"))).readDefaultParams());
}

BOOST_AUTO_TEST_CASE(test_projects_without_processing_options_take_the_fallback) {
  // As a project of ScanTailor Advanced or from before these settings were stored.
  const QDomDocument doc = parse(QStringLiteral("<project version=\"10\"/>"));
  const ProjectReader reader(doc);

  ProcessingOptions fallback;
  fallback.tiffCompression.color = COMPRESSION_JPEG;
  fallback.colorDetection.minColorShare = 0.1;
  fallback.blackOnWhiteAtOutput = false;
  fallback.pdf.ocrLanguages = QStringList{"ita"};
  const ProcessingOptions read = reader.readProcessingOptions(fallback);
  BOOST_CHECK_EQUAL(read.tiffCompression.color, COMPRESSION_JPEG);
  BOOST_CHECK(read.colorDetection == fallback.colorDetection);
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

namespace {
const QString kProjectStart(
    "<project version=\"4\" outputDirectory=\"/out\"><directories><directory id=\"1\" path=\"/in\"/></directories>"
    "<files><file id=\"2\" dirId=\"1\" name=\"a.png\"/></files>");
}  // namespace

BOOST_AUTO_TEST_CASE(test_missing_pages_are_an_error) {
  // Without <pages>, the settings of all pages would be lost unnoticed.
  const ProjectReader reader(parse(
      kProjectStart + "<images><image id=\"3\" subPages=\"1\" fileId=\"2\" fileImage=\"0\"/></images></project>"));
  BOOST_CHECK(!reader.success());
}

BOOST_AUTO_TEST_CASE(test_duplicate_images_are_skipped) {
  const ProjectReader reader(parse(kProjectStart
                                   + "<images><image id=\"3\" subPages=\"1\" fileId=\"2\" fileImage=\"0\"/>"
                                     "<image id=\"4\" subPages=\"1\" fileId=\"2\" fileImage=\"0\"/></images>"
                                     "<pages/></project>"));
  BOOST_REQUIRE(reader.success());
  BOOST_CHECK_EQUAL(reader.pages()->numImages(), 1);
}

BOOST_AUTO_TEST_CASE(test_images_with_invalid_sub_pages_are_skipped) {
  // ProjectPages only takes one or two sub pages; with no other image the file is broken.
  const ProjectReader reader(
      parse(kProjectStart
            + "<images><image id=\"3\" subPages=\"3\" fileId=\"2\" fileImage=\"0\"/></images><pages/></project>"));
  BOOST_CHECK(!reader.success());
}

BOOST_AUTO_TEST_CASE(test_inserted_half_page_has_the_id_of_its_page) {
  // Right to left, an image whose right half was removed shows its left half. insertImage()
  // returned the right one, a page that doesn't exist.
  const ImageMetadata metadata(QSize(3000, 2000), Dpi(300, 300));
  ProjectPages pages({ImageInfo(ImageId("/in/a.png"), metadata, 1, false, false)}, Qt::RightToLeft);
  const std::vector<PageInfo> inserted = pages.insertImage(ImageInfo(ImageId("/in/b.png"), metadata, 1, false, true),
                                                           AFTER, ImageId("/in/a.png"), PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(inserted.size(), 1u);
  bool found = false;
  for (const PageInfo& page : pages.toPageSequence(PAGE_VIEW)) {
    found = found || (page.id() == inserted[0].id());
  }
  BOOST_CHECK(found);
}

BOOST_AUTO_TEST_CASE(test_removing_a_half_that_isnt_there_keeps_the_image) {
  // A selection from before the layout changed can name a half the image doesn't have (any more).
  const ImageMetadata metadata(QSize(3000, 2000), Dpi(300, 300));
  ProjectPages pages({ImageInfo(ImageId("/in/single.png"), metadata, 1, false, false),
                      ImageInfo(ImageId("/in/half.png"), metadata, 1, true, false)},
                     Qt::LeftToRight);
  pages.removePages(
      {PageId(ImageId("/in/single.png"), PageId::LEFT_PAGE), PageId(ImageId("/in/half.png"), PageId::LEFT_PAGE)});
  BOOST_CHECK_EQUAL(pages.numImages(), 2);

  // The half that is left can still be removed, and then the image goes.
  pages.removePages({PageId(ImageId("/in/half.png"), PageId::RIGHT_PAGE)});
  BOOST_CHECK_EQUAL(pages.numImages(), 1);
}

BOOST_AUTO_TEST_CASE(test_metadata_update_reports_a_change) {
  // Without modified(), a DPI correction wasn't saved automatically.
  const QString file = QDir(QDir::tempPath()).absoluteFilePath("dpi.png");
  ProjectPages pages({ImageInfo(ImageId(file), ImageMetadata(QSize(2000, 3000), Dpi(300, 300)), 1, false, false)},
                     Qt::LeftToRight);
  int modifications = 0;
  QObject::connect(&pages, &ProjectPages::modified, [&modifications] { ++modifications; });

  std::vector<ImageFileInfo> files = pages.toImageFileInfo();
  pages.updateMetadataFrom(files);
  BOOST_CHECK_EQUAL(modifications, 0);
  files[0].imageInfo()[0] = ImageMetadata(QSize(2000, 3000), Dpi(600, 600));
  pages.updateMetadataFrom(files);
  BOOST_CHECK_EQUAL(modifications, 1);
}

BOOST_AUTO_TEST_CASE(test_image_file_info_keeps_the_page_numbers) {
  // With page 1 removed, the DPI dialog must still call the others pages 2 and 3.
  const QString multiPage = QDir(QDir::tempPath()).absoluteFilePath("pages.tif");
  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  const ProjectPages pages({ImageInfo(ImageId(multiPage, 2), metadata, 1, false, false),
                            ImageInfo(ImageId(multiPage, 3), metadata, 1, false, false)},
                           Qt::LeftToRight);
  const std::vector<ImageFileInfo> files = pages.toImageFileInfo();
  BOOST_REQUIRE_EQUAL(files.size(), 1u);
  BOOST_CHECK(files[0].pageNumbers() == std::vector<int>({2, 3}));
}

// Only 0, 90, 180 and 270 are valid; anything else from a project file is brought back to them.
BOOST_AUTO_TEST_CASE(test_rotation_from_xml_is_normalized) {
  const std::vector<std::pair<QString, int>> cases
      = {{"90", 90}, {"360", 0}, {"-90", 270}, {"450", 90}, {"45", 0}, {"abc", 0}};
  for (const auto& [degrees, expected] : cases) {
    QDomDocument doc;
    QDomElement el = doc.createElement("rotation");
    el.setAttribute("degrees", degrees);
    BOOST_CHECK_MESSAGE(OrthogonalRotation(el).toDegrees() == expected, degrees.toStdString());
  }
}

// A malformed value in a profile must not switch despeckling off.
BOOST_AUTO_TEST_CASE(test_malformed_despeckle_level_falls_back) {
  QDomDocument doc;
  QDomElement el = doc.createElement("output");
  el.setAttribute("despeckleLevel", "strong");
  BOOST_CHECK_EQUAL(DefaultParams::OutputParams(el).getDespeckleLevel(), 1.0);
}

BOOST_AUTO_TEST_CASE(test_deskew_mode_disabled_round_trip) {
  QDomDocument doc;
  const DefaultParams::DeskewParams params(0.0, MODE_DISABLED);
  const DefaultParams::DeskewParams read(params.toXml(doc, "deskew"));
  BOOST_CHECK(read.getMode() == MODE_DISABLED);
}
BOOST_AUTO_TEST_SUITE_END()
