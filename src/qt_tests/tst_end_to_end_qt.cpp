// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_end_to_end_qt.h"

#include <CompositeCacheDrivenTask.h>
#include <DefaultParams.h>
#include <Dpm.h>
#include <ExportJob.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <ImageSizeLimit.h>
#include <NullTaskStatus.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectSession.h>
#include <StageSequence.h>
#include <filters/deskew/Params.h>
#include <filters/deskew/Settings.h>
#include <filters/output/GeometryCollector.h>
#include <filters/output/Params.h>
#include <filters/output/Settings.h>
#include <filters/page_split/Params.h>
#include <filters/page_split/Settings.h>
#include <filters/select_content/Params.h>
#include <filters/select_content/Settings.h>
#include <tiffio.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "core/tests/ScopedMaxThreads.h"
#include "core/tests/TestTiffFile.h"
#include "test_project.h"

using namespace test_pages;

namespace {
// Read with libtiff, as the test has no TIFF plugin of Qt.
struct TiffInfo {
  bool valid = false;
  QSize size;
  double dpi = 0;
  int compression = -1;
  // The share of dark pixels, so that an empty or black page is noticed.
  double darkShare = -1;
};

double readDarkShare(TIFF* tif, const QSize& size) {
  const size_t numPixels = static_cast<size_t>(size.width()) * size.height();
  if (numPixels == 0) {
    return -1;
  }
  std::vector<uint32_t> raster(numPixels);
  if (TIFFReadRGBAImage(tif, size.width(), size.height(), raster.data(), 0) != 1) {
    return -1;
  }
  size_t dark = 0;
  for (const uint32_t pixel : raster) {
    const int gray = (TIFFGetR(pixel) * 11 + TIFFGetG(pixel) * 16 + TIFFGetB(pixel) * 5) / 32;
    if (gray < 128) {
      ++dark;
    }
  }
  return static_cast<double>(dark) / numPixels;
}

TiffInfo readTiff(const QString& filePath) {
  TiffInfo info;
  TIFF* tif = Tests::openTiffFile(filePath, "r");
  if (!tif) {
    return info;
  }
  uint32_t width = 0;
  uint32_t height = 0;
  float xResolution = 0;
  uint16_t unit = RESUNIT_INCH;
  uint16_t compression = 0;
  info.valid = (TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) == 1)
               && (TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height) == 1)
               && (TIFFGetField(tif, TIFFTAG_XRESOLUTION, &xResolution) == 1);
  TIFFGetFieldDefaulted(tif, TIFFTAG_RESOLUTIONUNIT, &unit);
  TIFFGetFieldDefaulted(tif, TIFFTAG_COMPRESSION, &compression);
  info.size = QSize(static_cast<int>(width), static_cast<int>(height));
  if (info.valid) {
    info.darkShare = readDarkShare(tif, info.size);
  }
  TIFFClose(tif);
  info.dpi = (unit == RESUNIT_CENTIMETER) ? xResolution * 2.54 : xResolution;
  info.compression = compression;
  return info;
}

QByteArray readFile(const QString& filePath) {
  QFile file(filePath);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// Exports the pages as the app does, in this thread. Returns the PDF, or nothing on failure.
QByteArray exportPdf(const std::vector<ExportPage>& pages,
                     const QString& filePath,
                     const ExportOptions& options,
                     QStringList* errors = nullptr) {
  ExportJob job(pages, ExportPaths::forPdfFile(filePath), options);
  job.run();
  if (errors) {
    *errors = job.errors();
  }
  return job.succeeded() ? readFile(filePath) : QByteArray();
}

// A complete file whose cross-reference table points at the start of each object, as a reader
// needs it to find the pages.
bool isCompletePdf(const QByteArray& pdf) {
  if (!pdf.startsWith("%PDF-") || !pdf.trimmed().endsWith("%%EOF")) {
    return false;
  }
  const qsizetype startxref = pdf.lastIndexOf("startxref\n");
  if (startxref < 0) {
    return false;
  }
  const qsizetype xrefOffset = pdf.mid(startxref + 10).split('\n').front().toLongLong();
  if (pdf.mid(xrefOffset, 7) != "xref\n0 ") {
    return false;
  }
  const qsizetype countEnd = pdf.indexOf('\n', xrefOffset + 7);
  const int count = pdf.mid(xrefOffset + 7, countEnd - xrefOffset - 7).toInt();
  // Each entry is 20 bytes long; entry 0 is the free one.
  for (int id = 1; id < count; ++id) {
    const qsizetype offset = pdf.mid(countEnd + 1 + id * 20, 10).toLongLong();
    const QByteArray objectStart = QByteArray::number(id) + " 0 obj";
    if (pdf.mid(offset, objectStart.size()) != objectStart) {
      return false;
    }
  }
  return count > 1;
}
}  // namespace

void TestEndToEndQt::testAllSteps() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files = saveScans(dir);
  QVERIFY(allSaved(files));

  const DefaultParams startValues;
  TestProject project(files, dir.absoluteFilePath("out"), startValues);
  QElapsedTimer timer;
  timer.start();
  QVERIFY(project.processAll());
  qInfo("All six steps for %d pages: %lld ms", static_cast<int>(project.pages->toPageSequence(PAGE_VIEW).numPages()),
        static_cast<long long>(timer.elapsed()));

  // The spread is split into two pages.
  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  QCOMPARE(pages.numPages(), size_t(4));
  const PageId& skewedId = pages.pageAt(0).id();
  QCOMPARE(skewedId.imageId().filePath(), files[0].fileInfo().absoluteFilePath());
  QCOMPARE(pages.pageAt(1).id().imageId(), pages.pageAt(2).id().imageId());

  // It is split at the dark gutter in the middle, not somewhere in the text.
  const page_split::Settings::Record splitRecord
      = project.stages->pageSplitFilter()->settings()->getPageRecord(pages.pageAt(1).id().imageId());
  QVERIFY(splitRecord.params());
  const page_split::PageLayout& layout = splitRecord.params()->pageLayout();
  QCOMPARE(layout.type(), page_split::PageLayout::TWO_PAGES);
  const QPointF splitCenter = layout.inscribedCutterLine(0).center();
  QVERIFY2(std::abs(splitCenter.x() - PAGE_SIZE.width()) <= 20,
           qPrintable(QString("Split line at x = %1").arg(splitCenter.x(), 0, 'f', 1)));

  // The angle the text was turned by is found, as the angle that turns it back.
  const std::unique_ptr<deskew::Params> deskewParams
      = project.stages->deskewFilter()->settings()->getPageParams(skewedId);
  QVERIFY(deskewParams);
  qInfo("Deskew angle: %.3f", deskewParams->deskewAngle());
  QVERIFY2(std::abs(deskewParams->deskewAngle() + SKEW_DEG) <= 0.2,
           qPrintable(QString("Deskew angle %1").arg(deskewParams->deskewAngle(), 0, 'f', 3)));

  // The content box holds the text, not the dark border.
  const std::unique_ptr<select_content::Params> contentParams
      = project.stages->selectContentFilter()->settings()->getPageParams(skewedId);
  QVERIFY(contentParams);
  const QRectF content = contentParams->contentRect();
  qInfo("Content box: %.0f x %.0f", content.width(), content.height());
  const QString contentSize = QString("Content box %1 x %2").arg(content.width()).arg(content.height());
  QVERIFY2(content.width() >= TEXT_RECT.width() * 0.9, qPrintable(contentSize));
  QVERIFY2(content.width() <= TEXT_RECT.width() * 1.1, qPrintable(contentSize));
  QVERIFY2(content.height() >= TEXT_RECT.height() * 0.9, qPrintable(contentSize));
  QVERIFY2(content.height() <= TEXT_RECT.height() * 1.1, qPrintable(contentSize));

  // Each page has its output file, with the resolution of the start values,
  // black and white as by default, so compressed as the project says and with the text on it.
  const Dpi outputDpi = startValues.getOutputParams().getDpi();
  for (const PageInfo& page : pages) {
    const QString filePath = project.outFileNameGen.filePathFor(page.id());
    const TiffInfo output = readTiff(filePath);
    QVERIFY2(output.valid, qPrintable(filePath));
    QCOMPARE(qRound(output.dpi), outputDpi.horizontal());
    QVERIFY(output.size.height() > output.size.width());
    QCOMPARE(output.compression, project.stages->processingOptions()->tiffCompression.bw);
    // The text covers about a quarter of its box, and the margins add white around it (about 17 %).
    // An empty page, or one that kept the dark border of the scan, lies outside.
    QVERIFY2(output.darkShare >= 0.10 && output.darkShare <= 0.25,
             qPrintable(QString("%1: %2 % dark pixels")
                            .arg(QFileInfo(filePath).fileName())
                            .arg(output.darkShare * 100, 0, 'f', 1)));
  }

  // "Detect Color Mode" as in the app: the geometry comes from the steps before.
  const std::shared_ptr<CompositeCacheDrivenTask> geometryTask
      = project.stages->createCacheDrivenTask(project.stages->outputFilterIdx(), project.outFileNameGen);
  const auto detect = [&](const PageInfo& page, output::ColorMode* mode) {
    output::GeometryCollector geometry;
    geometryTask->process(page, &geometry);
    return geometry.hasGeometry()
           && project.stages->outputFilter()->detectColorMode(page, geometry, NullTaskStatus(), mode);
  };
  // Each mode starts as one that is not expected, so a mode left unset fails as well.
  output::ColorMode textMode = output::MIXED;
  QVERIFY(detect(pages.pageAt(0), &textMode));
  QCOMPARE(textMode, output::BLACK_AND_WHITE);
  output::ColorMode pictureMode = output::BLACK_AND_WHITE;
  QVERIFY(detect(pages.pageAt(3), &pictureMode));
  QCOMPARE(pictureMode, output::MIXED);

  // Saved and loaded again, the project is the same, without processing anything again.
  const QString firstFile = dir.absoluteFilePath("first.ScanTailor");
  const QString secondFile = dir.absoluteFilePath("second.ScanTailor");
  QVERIFY(project.write(firstFile));
  const ProjectReader::FileResult read = ProjectReader::readFile(firstFile);
  QVERIFY(read.reader);
  const ProjectSession loaded = ProjectSession::load(*read.reader, DefaultParams());
  QVERIFY(loaded.write(secondFile));
  QCOMPARE(readFile(secondFile), readFile(firstFile));
}

void TestEndToEndQt::testColorOutputCompression() {
  // Color output is compressed as the project says, here as JPEG.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(colorPage(), dir.absoluteFilePath("color.png"))};
  QVERIFY(allSaved(files));

  DefaultParams startValues;
  DefaultParams::OutputParams outputParams = startValues.getOutputParams();
  output::ColorParams colorParams = outputParams.getColorParams();
  colorParams.setColorMode(output::COLOR_GRAYSCALE);
  outputParams.setColorParams(colorParams);
  startValues.setOutputParams(outputParams);
  ProcessingOptions processingOptions = startValues.getProcessingOptions();
  processingOptions.tiffCompression.color = COMPRESSION_JPEG;
  startValues.setProcessingOptions(processingOptions);

  TestProject project(files, dir.absoluteFilePath("out"), startValues);
  QVERIFY(project.processAll());

  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  QCOMPARE(pages.numPages(), size_t(1));
  const TiffInfo output = readTiff(project.outFileNameGen.filePathFor(pages.pageAt(0).id()));
  QVERIFY(output.valid);
  QCOMPARE(output.compression, COMPRESSION_JPEG);

  // In the PDF, a color page is compressed as the project says, by default as JPEG.
  QCOMPARE(project.stages->processingOptions()->pdf.colorCompression, PdfCompression::JPEG);
  const QByteArray pdf
      = exportPdf(project.stages->pdfExportPages(*project.pages, project.outFileNameGen),
                  dir.absoluteFilePath("color.pdf"), ExportOptions::fromProject(*project.stages->processingOptions()));
  QVERIFY(isCompletePdf(pdf));
  QVERIFY(pdf.contains("/Filter /DCTDecode"));
}

void TestEndToEndQt::testMissingImage() {
  // An image that can't be loaded is reported, and as the size of its pages is unknown, no page
  // is output.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files = saveScans(dir);
  const QString missingFile = files[1].fileInfo().absoluteFilePath();
  QVERIFY(QFile::remove(missingFile));

  const QString outDir = dir.absoluteFilePath("out");
  TestProject project(files, outDir, DefaultParams());
  const StageSequence::ProcessAllResult result
      = project.stages->processAll(project.thumbnailCache, project.outFileNameGen, project.stages->outputFilterIdx());
  QVERIFY(!result.output);
  QVERIFY(!result.cancelled);
  QCOMPARE(result.failures.size(), size_t(1));
  QCOMPARE(result.failures[0].kind, StageSequence::PageFailure::LOAD_FAILED);
  QCOMPARE(result.failures[0].page.imageId().filePath(), missingFile);
  QVERIFY(QDir(outDir).entryList({"*.tif"}, QDir::Files).isEmpty());
}

void TestEndToEndQt::testMissingImageAfterEarlierRun() {
  // After an earlier run the size of all pages is known. An image that fails now must still stop
  // the output, or its old output files would end up in the PDF as if they were current.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files = saveScans(dir);
  QVERIFY(allSaved(files));
  TestProject project(files, dir.absoluteFilePath("out"), DefaultParams());
  QVERIFY(project.processAll());

  QVERIFY(QFile::remove(files[2].fileInfo().absoluteFilePath()));
  const StageSequence::ProcessAllResult result
      = project.stages->processAll(project.thumbnailCache, project.outFileNameGen, project.stages->outputFilterIdx());
  QVERIFY(!result.output);
  QCOMPARE(result.failures.size(), size_t(1));
  QCOMPARE(result.failures[0].kind, StageSequence::PageFailure::LOAD_FAILED);
}

void TestEndToEndQt::testOutputTooLarge() {
  // An output resolution that makes the pages exceed the size limit is a matter of the settings,
  // not an unexpected error: processAll() says so, with the message naming what to change.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(colorPage(), dir.absoluteFilePath("page.png"))};
  QVERIFY(allSaved(files));

  DefaultParams startValues;
  DefaultParams::OutputParams outputParams = startValues.getOutputParams();
  outputParams.setDpi(Dpi(100000, 100000));
  startValues.setOutputParams(outputParams);

  TestProject project(files, dir.absoluteFilePath("out"), startValues);
  const StageSequence::ProcessAllResult result
      = project.stages->processAll(project.thumbnailCache, project.outFileNameGen, project.stages->outputFilterIdx());
  QVERIFY(!result.failures.empty());
  for (const StageSequence::PageFailure& failure : result.failures) {
    QCOMPARE(failure.kind, StageSequence::PageFailure::OUTPUT_TOO_LARGE);
    QCOMPARE(failure.reasons.size(), qsizetype(1));
    QVERIFY(failure.reasons[0].contains(QString::number(ImageSizeLimit::MAX_SIDE)));
  }
}

void TestEndToEndQt::testSplitTooLarge() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(twoPageSpread(), dir.absoluteFilePath("spread.png"))};
  QVERIFY(allSaved(files));

  DefaultParams startValues;
  DefaultParams::OutputParams outputParams = startValues.getOutputParams();
  outputParams.setDpi(Dpi(100000, 100000));
  startValues.setOutputParams(outputParams);

  PageId pageWithMax1, pageWithMax4;
  {
    const Tests::ScopedMaxThreads threads(1);
    TestProject project(files, dir.absoluteFilePath("out1"), startValues);
    const StageSequence::ProcessAllResult result
        = project.stages->processAll(project.thumbnailCache, project.outFileNameGen, project.stages->outputFilterIdx());
    QCOMPARE(result.failures.size(), qsizetype(1));
    pageWithMax1 = result.failures.front().page;
  }
  {
    const Tests::ScopedMaxThreads threads(4);
    TestProject project(files, dir.absoluteFilePath("out4"), startValues);
    const StageSequence::ProcessAllResult result
        = project.stages->processAll(project.thumbnailCache, project.outFileNameGen, project.stages->outputFilterIdx());
    QCOMPARE(result.failures.size(), qsizetype(1));
    pageWithMax4 = result.failures.front().page;
  }
  QCOMPARE(pageWithMax1, pageWithMax4);
}

void TestEndToEndQt::testCancelWithoutThumbnails() {
  // Without thumbnails, processing can be cancelled between two pages and run again.
  // This test checks the serial ordering of pages, so it runs with one thread.
  const Tests::ScopedMaxThreads threads(1);
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(skewedPage(), dir.absoluteFilePath("1-skewed.png")),
                                         saveScan(colorPage(), dir.absoluteFilePath("2-color.png"))};
  QVERIFY(allSaved(files));
  TestProject project(files, dir.absoluteFilePath("out"), DefaultParams());
  project.thumbnailCache.reset();
  const int outputIdx = project.stages->outputFilterIdx();

  // The cancel request comes from the progress of the output, after its first page. How often the
  // steps before report their progress is not checked here.
  bool cancelRequested = false;
  const StageSequence::ProcessAllResult cancelled = project.stages->processAll(
      nullptr, project.outFileNameGen, project.stages->outputFilterIdx(), [&](const StageSequence::PageProgress& p) {
        if (p.lastFilterIdx == outputIdx && p.done >= 1 && p.done < p.total) {
          cancelRequested = true;
          return false;
        }
        return true;
      });
  QVERIFY(cancelRequested);
  QVERIFY(cancelled.cancelled);
  QVERIFY(!cancelled.output);
  QVERIFY(cancelled.failures.empty());
  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  QVERIFY(QFile::exists(project.outFileNameGen.filePathFor(pages.pageAt(0).id())));
  QVERIFY(!QFile::exists(project.outFileNameGen.filePathFor(pages.pageAt(1).id())));

  QVERIFY(project.processAll());
  QVERIFY(QFile::exists(project.outFileNameGen.filePathFor(pages.pageAt(1).id())));
}

void TestEndToEndQt::testPdfExport() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files = saveScans(dir);
  QVERIFY(allSaved(files));
  TestProject project(files, dir.absoluteFilePath("out"), DefaultParams());
  QVERIFY(project.processAll());

  // One page of the PDF for each page of the project, in the order of the project.
  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  const std::vector<ExportPage> pdfPages = project.stages->pdfExportPages(*project.pages, project.outFileNameGen);
  QCOMPARE(pdfPages.size(), pages.numPages());
  for (size_t i = 0; i < pdfPages.size(); ++i) {
    QCOMPARE(pdfPages[i].mainFile(), project.outFileNameGen.filePathFor(pages.pageAt(i).id()));
    QCOMPARE(pdfPages[i].pageId(), pages.pageAt(i).id());
    QVERIFY(!pdfPages[i].isOutdated());
  }

  // Black and white pages are compressed as the project says: JBIG2 by default, CCITT G4 if set.
  ProcessingOptions processingOptions = *project.stages->processingOptions();
  QCOMPARE(processingOptions.pdf.bitonalCompression, PdfCompression::JBIG2);
  const QString pdfFile = dir.absoluteFilePath("book.pdf");
  QElapsedTimer timer;
  timer.start();
  QByteArray pdf = exportPdf(pdfPages, pdfFile, ExportOptions::fromProject(processingOptions));
  qInfo("PDF export of %d pages: %lld ms", static_cast<int>(pdfPages.size()), static_cast<long long>(timer.elapsed()));
  QVERIFY(isCompletePdf(pdf));
  QVERIFY(pdf.contains("/Count 4 "));
  QCOMPARE(pdf.count("/Filter /JBIG2Decode"), 4);

  processingOptions.pdf.bitonalCompression = PdfCompression::CCITT_G4;
  pdf = exportPdf(pdfPages, pdfFile, ExportOptions::fromProject(processingOptions));
  QVERIFY(isCompletePdf(pdf));
  QCOMPARE(pdf.count("/Filter /CCITTFaxDecode"), 4);

  // The size of a page follows from the output file and its resolution.
  const TiffInfo first = readTiff(pdfPages.front().mainFile());
  QVERIFY(first.valid);
  // The resolution is stored in dots per meter, so the size may differ in the third decimal.
  const QRegularExpressionMatch mediaBox
      = QRegularExpression(R"(/MediaBox \[0 0 ([0-9.]+) ([0-9.]+)\])").match(QString::fromLatin1(pdf));
  QVERIFY(mediaBox.hasMatch());
  QVERIFY2(std::abs(mediaBox.captured(1).toDouble() - first.size.width() * 72.0 / first.dpi) < 0.01,
           qPrintable(mediaBox.captured(0)));
  QVERIFY2(std::abs(mediaBox.captured(2).toDouble() - first.size.height() * 72.0 / first.dpi) < 0.01,
           qPrintable(mediaBox.captured(0)));

  // A missing output file makes the export fail with a message, and the PDF from before stays as it was.
  std::vector<ExportPage> withMissingFile = pdfPages;
  withMissingFile[1] = ExportPage(dir.absoluteFilePath("out/missing.tif"), QString(), QString(), false);
  withMissingFile[1].analyze();
  QStringList errors;
  QVERIFY(exportPdf(withMissingFile, pdfFile, ExportOptions::fromProject(processingOptions), &errors).isEmpty());
  QVERIFY(!errors.isEmpty());
  QCOMPARE(readFile(pdfFile), pdf);

  // A page changed in the Output step after the output is marked, like with the hourglass there.
  // Its output file still exists, so it can be exported as it is.
  const std::shared_ptr<output::Settings> outputSettings = project.stages->outputFilter()->settings();
  const PageId& changedId = pages.pageAt(1).id();
  const Dpi dpi = outputSettings->getParams(changedId).outputDpi();
  outputSettings->setDpi(changedId, Dpi(dpi.horizontal() + 100, dpi.vertical() + 100));
  const std::vector<ExportPage> afterChange = project.stages->pdfExportPages(*project.pages, project.outFileNameGen);
  for (size_t i = 0; i < afterChange.size(); ++i) {
    QCOMPARE(afterChange[i].isOutdated(), i == 1);
    QVERIFY(afterChange[i].kind() != ExportPage::MISSING);
  }
}

void TestEndToEndQt::testMixedSplitOutputMrcPdfAndAutoDewarping() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(colorPage(), dir.absoluteFilePath("mixed_dewarp.png"))};
  QVERIFY(allSaved(files));

  DefaultParams startValues;
  DefaultParams::OutputParams outputParams = startValues.getOutputParams();
  output::ColorParams colorParams = outputParams.getColorParams();
  colorParams.setColorMode(output::MIXED);
  outputParams.setColorParams(colorParams);
  output::SplittingOptions splitOpts = outputParams.getSplittingOptions();
  splitOpts.setSplitOutput(true);
  outputParams.setSplittingOptions(splitOpts);
  outputParams.setDewarpingOptions(output::DewarpingOptions(output::AUTO));
  startValues.setOutputParams(outputParams);

  TestProject project(files, dir.absoluteFilePath("out"), startValues);
  QVERIFY(project.processAll());

  const std::vector<ExportPage> pdfPages = project.stages->pdfExportPages(*project.pages, project.outFileNameGen);
  QCOMPARE(pdfPages.size(), size_t(1));
  QCOMPARE(pdfPages.front().kind(), ExportPage::MRC);
  QVERIFY(QFile::exists(pdfPages.front().mainFile()));
  QVERIFY(QFile::exists(pdfPages.front().foregroundFile()));
  QVERIFY(QFile::exists(pdfPages.front().backgroundFile()));

  const ProcessingOptions processingOptions = *project.stages->processingOptions();
  const QString pdfFile = dir.absoluteFilePath("mrc.pdf");
  const QByteArray pdf = exportPdf(pdfPages, pdfFile, ExportOptions::fromProject(processingOptions));
  QVERIFY(isCompletePdf(pdf));
  QVERIFY(pdf.contains("/Count 1 "));
  QVERIFY(pdf.contains("/ImageMask true"));
}
