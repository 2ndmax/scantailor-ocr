// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_end_to_end_qt.h"

#include <CompositeCacheDrivenTask.h>
#include <DefaultParams.h>
#include <Dpm.h>
#include <FileNameDisambiguator.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <NullTaskStatus.h>
#include <OutputFileNameGenerator.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <SelectedPage.h>
#include <StageSequence.h>
#include <ThumbnailPixmapCache.h>
#include <Utils.h>
#include <filters/deskew/Params.h>
#include <filters/deskew/Settings.h>
#include <filters/output/GeometryCollector.h>
#include <filters/select_content/Params.h>
#include <filters/select_content/Settings.h>
#include <tiffio.h>

#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <cmath>
#include <memory>
#include <vector>

namespace {
constexpr int DPI = 300;
constexpr double SKEW_DEG = 1.5;
const QSize PAGE_SIZE(1200, 1700);
const QRect TEXT_RECT(200, 250, 800, 1200);

// Lines of "words" of different lengths, like printed text.
void drawText(QPainter& painter, const QRect& rect) {
  for (int y = rect.top(); y + 6 <= rect.bottom(); y += 18) {
    int x = rect.left();
    while (true) {
      const int width = 20 + ((x * 7 + y * 3) % 40);
      if (x + width > rect.right()) {
        break;
      }
      painter.fillRect(QRect(x, y, width, 6), Qt::black);
      x += width + 12;
    }
  }
}

QImage blankImage(const QSize& size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(qRgb(250, 250, 250));
  const Dpm dpm(Dpi(DPI, DPI));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return image;
}

// A page with text turned by SKEW_DEG and a dark border all around, as from a scanner lid.
QImage skewedPage() {
  QImage image(blankImage(PAGE_SIZE));
  QPainter painter(&image);
  painter.fillRect(image.rect(), qRgb(40, 40, 40));
  painter.fillRect(image.rect().adjusted(60, 60, -60, -60), qRgb(250, 250, 250));
  const QPointF center(image.rect().center());
  painter.translate(center);
  painter.rotate(SKEW_DEG);
  painter.translate(-center);
  drawText(painter, TEXT_RECT);
  return image;
}

// Two pages side by side with a dark gutter between them.
QImage twoPageSpread() {
  QImage image(blankImage(QSize(PAGE_SIZE.width() * 2, PAGE_SIZE.height())));
  QPainter painter(&image);
  drawText(painter, TEXT_RECT);
  drawText(painter, TEXT_RECT.translated(PAGE_SIZE.width(), 0));
  painter.fillRect(QRect(PAGE_SIZE.width() - 10, 0, 20, PAGE_SIZE.height()), qRgb(60, 60, 60));
  return image;
}

// A colored picture above black text. The picture has edges like a photo; a smooth gradient
// would count as background, not as content.
QImage colorPage() {
  QImage image(blankImage(PAGE_SIZE));
  const QRect pictureRect(TEXT_RECT.left(), TEXT_RECT.top(), TEXT_RECT.width(), 500);
  QPainter painter(&image);
  constexpr int tile = 25;
  for (int y = pictureRect.top(); y < pictureRect.bottom(); y += tile) {
    for (int x = pictureRect.left(); x < pictureRect.right(); x += tile) {
      const int v = 40 + ((x * 13 + y * 7) % 170);
      painter.fillRect(QRect(x, y, tile, tile), QColor(v, 220 - v, 60 + (v * 3) % 150));
    }
  }
  drawText(painter, QRect(TEXT_RECT.left(), pictureRect.bottom() + 100, TEXT_RECT.width(), 600));
  return image;
}

ImageFileInfo saveScan(const QImage& image, const QString& filePath) {
  if (!image.save(filePath)) {
    return ImageFileInfo(QFileInfo(filePath), {});
  }
  return ImageFileInfo(QFileInfo(filePath), {ImageMetadata(image.size(), Dpi(DPI, DPI))});
}

// Read with libtiff, as the test has no TIFF plugin of Qt.
struct TiffInfo {
  bool valid = false;
  QSize size;
  double dpi = 0;
  int compression = -1;
};

TiffInfo readTiff(const QString& filePath) {
  TiffInfo info;
  TIFF* tif = TIFFOpen(QFile::encodeName(filePath).constData(), "r");
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
  TIFFClose(tif);
  info.size = QSize(static_cast<int>(width), static_cast<int>(height));
  info.dpi = (unit == RESUNIT_CENTIMETER) ? xResolution * 2.54 : xResolution;
  info.compression = compression;
  return info;
}

QByteArray readFile(const QString& filePath) {
  QFile file(filePath);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

// A project set up as the app sets up a new one, without its window.
class TestProject {
 public:
  TestProject(const std::vector<ImageFileInfo>& files, const QString& outDir, const DefaultParams& startValues)
      : pages(ProjectPages::createNew(files, startValues)),
        stages(std::make_shared<StageSequence>(pages, PageSelectionAccessor(nullptr))),
        outFileNameGen(std::make_shared<FileNameDisambiguator>(), outDir, pages->layoutDirection()) {
    QDir().mkpath(outDir);
    thumbnailCache = core::Utils::createThumbnailCache(outDir);
    stages->startNewProject(startValues);
  }

  /**
   * Processes the pages of \p view up to the step \p lastFilterIdx, as batch processing does.
   */
  void process(const PageView view, const int lastFilterIdx) {
    for (const PageInfo& page : pages->toPageSequence(view)) {
      stages->loadDefaultSettings(page);
      const BackgroundTaskPtr task
          = stages->createTask(page, lastFilterIdx, thumbnailCache, outFileNameGen, pages, true, false);
      (*task)();
    }
  }

  /**
   * All six steps. Which images have two pages is only known after the step that splits them,
   * so the pages are processed after the images, as in the app.
   */
  void processAll() {
    process(IMAGE_VIEW, stages->pageSplitFilterIdx());
    process(PAGE_VIEW, stages->outputFilterIdx());
  }

  std::shared_ptr<ProjectPages> pages;
  std::shared_ptr<StageSequence> stages;
  OutputFileNameGenerator outFileNameGen;
  std::shared_ptr<ThumbnailPixmapCache> thumbnailCache;
};
}  // namespace

void TestEndToEndQt::testAllSteps() {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(skewedPage(), dir.absoluteFilePath("1-skewed.png")),
                                         saveScan(twoPageSpread(), dir.absoluteFilePath("2-spread.png")),
                                         saveScan(colorPage(), dir.absoluteFilePath("3-color.png"))};
  for (const ImageFileInfo& file : files) {
    QVERIFY(!file.imageInfo().empty());
  }

  const DefaultParams startValues;
  TestProject project(files, dir.absoluteFilePath("out"), startValues);
  QElapsedTimer timer;
  timer.start();
  project.processAll();
  qInfo("All six steps for %d pages: %lld ms", static_cast<int>(project.pages->toPageSequence(PAGE_VIEW).numPages()),
        static_cast<long long>(timer.elapsed()));

  // The spread is split into two pages.
  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  QCOMPARE(pages.numPages(), size_t(4));
  const PageId& skewedId = pages.pageAt(0).id();
  QCOMPARE(skewedId.imageId().filePath(), files[0].fileInfo().absoluteFilePath());
  QCOMPARE(pages.pageAt(1).id().imageId(), pages.pageAt(2).id().imageId());

  // The angle the text was turned by is found, as the angle that turns it back.
  const std::unique_ptr<deskew::Params> deskewParams
      = project.stages->deskewFilter()->settings()->getPageParams(skewedId);
  QVERIFY(deskewParams);
  qInfo("Deskew angle: %.3f", deskewParams->deskewAngle());
  QVERIFY(std::abs(deskewParams->deskewAngle() + SKEW_DEG) <= 0.2);

  // The content box holds the text, not the dark border.
  const std::unique_ptr<select_content::Params> contentParams
      = project.stages->selectContentFilter()->settings()->getPageParams(skewedId);
  QVERIFY(contentParams);
  const QRectF content = contentParams->contentRect();
  qInfo("Content box: %.0f x %.0f", content.width(), content.height());
  QVERIFY(content.width() >= TEXT_RECT.width() * 0.9);
  QVERIFY(content.width() <= TEXT_RECT.width() * 1.1);
  QVERIFY(content.height() >= TEXT_RECT.height() * 0.9);
  QVERIFY(content.height() <= TEXT_RECT.height() * 1.1);

  // Each page has its output file, with the resolution of the start values,
  // black and white as by default and so compressed as the project says.
  const Dpi outputDpi = startValues.getOutputParams().getDpi();
  for (const PageInfo& page : pages) {
    const QString filePath = project.outFileNameGen.filePathFor(page.id());
    const TiffInfo output = readTiff(filePath);
    QVERIFY2(output.valid, qPrintable(filePath));
    QCOMPARE(qRound(output.dpi), outputDpi.horizontal());
    QVERIFY(output.size.height() > output.size.width());
    QCOMPARE(output.compression, project.stages->processingOptions()->tiffCompression.bw);
  }

#ifdef ENABLE_OCR
  // "Detect Color Mode" as in the app: the geometry comes from the steps before.
  const std::shared_ptr<CompositeCacheDrivenTask> geometryTask
      = project.stages->createCacheDrivenTask(project.stages->outputFilterIdx(), project.outFileNameGen);
  const auto detect = [&](const PageInfo& page) {
    output::GeometryCollector geometry;
    geometryTask->process(page, &geometry);
    output::ColorMode mode = output::BLACK_AND_WHITE;
    const bool detected = geometry.hasGeometry()
                          && project.stages->outputFilter()->detectColorMode(page, geometry, NullTaskStatus(), &mode);
    return detected ? mode : static_cast<output::ColorMode>(-1);
  };
  QCOMPARE(detect(pages.pageAt(0)), output::BLACK_AND_WHITE);
  QVERIFY(detect(pages.pageAt(3)) != output::BLACK_AND_WHITE);
#endif

  // Saved and loaded again, the project is the same, without processing anything again.
  const QString firstFile = dir.absoluteFilePath("first.ScanTailor");
  const QString secondFile = dir.absoluteFilePath("second.ScanTailor");
  QVERIFY(ProjectWriter(project.pages, SelectedPage(), project.outFileNameGen)
              .write(firstFile, project.stages->filters(), *project.stages->processingOptions(),
                     *project.stages->defaultParams()));
  QDomDocument doc;
  QFile file(firstFile);
  QVERIFY(file.open(QIODevice::ReadOnly));
  QVERIFY(static_cast<bool>(doc.setContent(&file)));
  const ProjectReader reader(doc, firstFile);
  QVERIFY(reader.success());
  StageSequence loaded(reader.pages(), PageSelectionAccessor(nullptr));
  loaded.loadProject(reader, DefaultParams());
  const OutputFileNameGenerator loadedNames(reader.namingDisambiguator(), reader.outputDirectory(),
                                            reader.pages()->layoutDirection());
  QVERIFY(ProjectWriter(reader.pages(), SelectedPage(), loadedNames)
              .write(secondFile, loaded.filters(), *loaded.processingOptions(), *loaded.defaultParams()));
  QCOMPARE(readFile(secondFile), readFile(firstFile));
}

void TestEndToEndQt::testColorOutputCompression() {
  // Color output is compressed as the project says, here as JPEG.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const std::vector<ImageFileInfo> files{saveScan(colorPage(), dir.absoluteFilePath("color.png"))};
  QVERIFY(!files[0].imageInfo().empty());

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
  project.processAll();

  const PageSequence pages = project.pages->toPageSequence(PAGE_VIEW);
  QCOMPARE(pages.numPages(), size_t(1));
  const TiffInfo output = readTiff(project.outFileNameGen.filePathFor(pages.pageAt(0).id()));
  QVERIFY(output.valid);
  QCOMPARE(output.compression, COMPRESSION_JPEG);
}
