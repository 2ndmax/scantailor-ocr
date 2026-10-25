// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_core_qt.h"

#include <ApplicationSettings.h>
#include <DefaultParams.h>
#include <Dpi.h>
#include <FileNameDisambiguator.h>
#include <ImageInfo.h>
#include <ImageTransformation.h>
#include <OutputFileNameGenerator.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <SelectedPage.h>
#include <SmartFilenameOrdering.h>
#include <Units.h>
#include <WorkerThreadPool.h>
#include <dewarping/Curve.h>
#include <dewarping/DistortionModel.h>
#include <filters/output/OptionsWidget.h>
#include <filters/output/Settings.h>
#include <filters/page_split/Filter.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/Thumbnail.h>
#include <foundation/Utils.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QSignalSpy>
#include <QSpinBox>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <stdexcept>
#include <vector>

void TestCoreQt::testUnitsToString() {
  QCOMPARE(unitsToString(PIXELS), QString("px"));
  QCOMPARE(unitsToString(MILLIMETRES), QString("mm"));
  QCOMPARE(unitsToString(CENTIMETRES), QString("cm"));
  QCOMPARE(unitsToString(INCHES), QString("in"));
}

void TestCoreQt::testUnitsFromString() {
  QCOMPARE(unitsFromString(QString("px")), PIXELS);
  QCOMPARE(unitsFromString(QString("mm")), MILLIMETRES);
  QCOMPARE(unitsFromString(QString("cm")), CENTIMETRES);
  QCOMPARE(unitsFromString(QString("in")), INCHES);
  QCOMPARE(unitsFromString(QString("unknown")), MILLIMETRES);
}

void TestCoreQt::testUnitsToLocalizedString() {
  QString px = unitsToLocalizedString(PIXELS);
  QString mm = unitsToLocalizedString(MILLIMETRES);
  QVERIFY(!px.isEmpty());
  QVERIFY(!mm.isEmpty());
}

void TestCoreQt::testUtilsDoubleToString() {
  QCOMPARE(foundation::Utils::doubleToString(0.0), QString("0"));
  QVERIFY(foundation::Utils::doubleToString(123.456).contains("123"));
  QVERIFY(foundation::Utils::doubleToString(-42.5).startsWith("-"));
  // Read back unchanged, as the project file needs it (0.1 * 12 is 1.2000000000000002).
  for (const double value : {0.1 * 12, 0.1 * 3, 1.0 / 3.0, 1e-300}) {
    QCOMPARE(foundation::Utils::doubleToString(value).toDouble(), value);
  }
}

void TestCoreQt::testSmartFilenameOrdering() {
  const SmartFilenameOrdering less;
  const QFileInfo a("/etc/1.png");
  const QFileInfo b("/etc/2.png");
  QVERIFY(less(a, b));
  QVERIFY(!less(b, a));
  QVERIFY(!less(a, a));

  const QFileInfo c("/etc/a_0002.png");
  const QFileInfo d("/etc/a_1.png");
  QVERIFY(less(d, c));
}

void TestCoreQt::testSignalSpy() {
  Emitter obj;
  QSignalSpy spy(&obj, &Emitter::valueChanged);
  QVERIFY(spy.isValid());
  QCOMPARE(spy.count(), 0);

  obj.doEmit(QString("hello"));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QString("hello"));

  obj.doEmit(QString("world"));
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(0).toString(), QString("world"));
}

namespace {
QDomDocument readDocument(const QString& filePath) {
  QDomDocument doc;
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    doc.setContent(&file);
  }
  return doc;
}
}  // namespace

void TestCoreQt::testPageSplitKeepsLayoutTypeWithoutParams() {
  // A page type chosen for a page that wasn't processed yet has no split line (params).
  // It must still be saved.
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const QString firstFile = dir.absoluteFilePath("first.ScanTailor");
  const QString secondFile = dir.absoluteFilePath("second.ScanTailor");
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), dir.absoluteFilePath("out"),
                                       Qt::LeftToRight);

  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  auto pages = std::make_shared<ProjectPages>(
      std::vector<ImageInfo>{ImageInfo(ImageId(dir.absoluteFilePath("scan.png"), 0), metadata, 1, false, false)},
      Qt::LeftToRight);
  QVERIFY(ProjectWriter(pages, SelectedPage(), outGen).write(firstFile, {}, ProcessingOptions(), DefaultParams()));

  QDomDocument doc = readDocument(firstFile);
  QDomElement projectEl = doc.documentElement();
  const QString imageId = projectEl.namedItem("images").firstChildElement("image").attribute("id");
  QVERIFY(!imageId.isEmpty());
  QDomElement filtersEl = projectEl.namedItem("filters").toElement();
  if (filtersEl.isNull()) {
    filtersEl = doc.createElement("filters");
    projectEl.appendChild(filtersEl);
  }
  QDomElement pageSplitEl = doc.createElement("page-split");
  pageSplitEl.setAttribute("defaultLayoutType", "auto-detect");
  QDomElement imageEl = doc.createElement("image");
  imageEl.setAttribute("id", imageId);
  imageEl.setAttribute("layoutType", "single-uncut");
  pageSplitEl.appendChild(imageEl);
  filtersEl.appendChild(pageSplitEl);

  const ProjectReader reader(doc, firstFile);
  QVERIFY(reader.success());
  auto filter = std::make_shared<page_split::Filter>(reader.pages(), PageSelectionAccessor(nullptr));
  reader.readFilterSettings({filter});
  QVERIFY(ProjectWriter(reader.pages(), SelectedPage(), outGen)
              .write(secondFile, {filter}, ProcessingOptions(), DefaultParams()));

  const QDomElement savedEl = readDocument(secondFile)
                                  .documentElement()
                                  .namedItem("filters")
                                  .namedItem("page-split")
                                  .firstChildElement("image");
  QVERIFY(!savedEl.isNull());
  QCOMPARE(savedEl.attribute("layoutType"), QString("single-uncut"));
}

void TestCoreQt::testOutputDisplayKeepsColorEqualization() {
  // Showing a page in mixed mode must not change its color setting; it only doesn't apply while
  // the black and white equalization is off.
  auto settings = std::make_shared<output::Settings>();
  const PageId page(ImageId("page.png"));
  output::ColorParams colorParams;
  colorParams.setColorMode(output::MIXED);
  output::BlackWhiteOptions blackWhiteOptions(colorParams.blackWhiteOptions());
  blackWhiteOptions.setNormalizeIllumination(false);
  colorParams.setBlackWhiteOptions(blackWhiteOptions);
  output::ColorCommonOptions colorCommonOptions(colorParams.colorCommonOptions());
  colorCommonOptions.setNormalizeIllumination(true);
  colorParams.setColorCommonOptions(colorCommonOptions);
  settings->setColorParams(page, colorParams);

  output::OptionsWidget widget(settings, std::make_shared<ProcessingOptions>(), PageSelectionAccessor(nullptr));
  widget.preUpdateUI(page);

  QVERIFY(settings->getParams(page).colorParams().colorCommonOptions().normalizeIllumination());
  auto* colorCB = widget.findChild<QCheckBox*>("equalizeIlluminationColorCB");
  QVERIFY(colorCB != nullptr);
  QVERIFY(!colorCB->isChecked());
  QVERIFY(!colorCB->isEnabled());
}

void TestCoreQt::testOutputDpiChangeUpdatesDewarpingMode() {
  // A new resolution turns automatic dewarping with curves into manual; a later dewarping change
  // on the same page must keep that.
  auto settings = std::make_shared<output::Settings>();
  const PageId page(ImageId("page.png"));
  dewarping::DistortionModel model;
  model.setTopCurve(dewarping::Curve(std::vector<QPointF>{QPointF(0, 0), QPointF(100, 0)}));
  model.setBottomCurve(dewarping::Curve(std::vector<QPointF>{QPointF(0, 100), QPointF(100, 100)}));
  QVERIFY(model.isValid());
  settings->setDistortionModel(page, model);
  settings->setDewarpingOptions(page, output::DewarpingOptions(output::AUTO));

  output::OptionsWidget widget(settings, std::make_shared<ProcessingOptions>(), PageSelectionAccessor(nullptr));
  widget.preUpdateUI(page);

  auto* dpiSelector = widget.findChild<QComboBox*>("dpiSelector");
  QVERIFY(dpiSelector != nullptr);
  dpiSelector->setEditText("300");
  emit dpiSelector->lineEdit()->editingFinished();
  QCOMPARE(settings->getParams(page).outputDpi(), Dpi(300, 300));
  QCOMPARE(settings->getParams(page).dewarpingOptions().dewarpingMode(), output::MANUAL);

  auto* evenOutCB = widget.findChild<QCheckBox*>("evenOutLetterWidthsCB");
  QVERIFY(evenOutCB != nullptr);
  evenOutCB->click();
  QCOMPARE(settings->getParams(page).dewarpingOptions().dewarpingMode(), output::MANUAL);
}

void TestCoreQt::testOutputPageSwitchDropsDelayedReload() {
  // A delayed reload of the previous page must not reload the next one; the previous page's
  // thumbnail is redone instead.
  auto settings = std::make_shared<output::Settings>();
  const PageId first(ImageId("first.png"));
  const PageId second(ImageId("second.png"));

  output::OptionsWidget widget(settings, std::make_shared<ProcessingOptions>(), PageSelectionAccessor(nullptr));
  widget.preUpdateUI(first);

  QSignalSpy reloadSpy(&widget, &FilterOptionsWidget::reloadRequested);
  QSignalSpy thumbnailSpy(&widget, qOverload<const PageId&>(&FilterOptionsWidget::invalidateThumbnail));

  auto* sensitivitySB = widget.findChild<QSpinBox*>("pictureShapeSensitivitySB");
  QVERIFY(sensitivitySB != nullptr);
  sensitivitySB->setValue((sensitivitySB->value() > sensitivitySB->minimum()) ? sensitivitySB->value() - 1
                                                                              : sensitivitySB->value() + 1);
  widget.preUpdateUI(second);

  QCOMPARE(thumbnailSpy.count(), 1);
  QCOMPARE(thumbnailSpy.at(0).at(0).value<PageId>(), first);
  QTest::qWait(1000);
  QCOMPARE(reloadSpy.count(), 0);
}

void TestCoreQt::testOutputPageSwitchRedoesPreviousThumbnail() {
  // The processing of the previous page may have been cancelled by the switch, so its thumbnail
  // is redone. Loading the same page again doesn't do that.
  auto settings = std::make_shared<output::Settings>();
  const PageId first(ImageId("first.png"));
  const PageId second(ImageId("second.png"));

  output::OptionsWidget widget(settings, std::make_shared<ProcessingOptions>(), PageSelectionAccessor(nullptr));
  QSignalSpy thumbnailSpy(&widget, qOverload<const PageId&>(&FilterOptionsWidget::invalidateThumbnail));

  widget.preUpdateUI(first);
  widget.preUpdateUI(first);
  QCOMPARE(thumbnailSpy.count(), 0);

  widget.preUpdateUI(second);
  QCOMPARE(thumbnailSpy.count(), 1);
  QCOMPARE(thumbnailSpy.at(0).at(0).value<PageId>(), first);
}

namespace {
class ThrowingTask : public BackgroundTask {
 public:
  ThrowingTask() : BackgroundTask(BATCH) {}

  FilterResultPtr operator()() override { throw std::runtime_error("test failure"); }
};
}  // namespace

void TestCoreQt::testFailedTaskReportsBack() {
  // A task that fails with an exception has no result. It must report back anyway, or the batch
  // processing waits for it forever.
  WorkerThreadPool pool;
  QSignalSpy spy(&pool, &WorkerThreadPool::taskResult);
  pool.submitTask(std::make_shared<ThrowingTask>());
  QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 5000);
  QVERIFY(!spy.at(0).at(1).value<FilterResultPtr>());
  pool.shutdown();
}

void TestCoreQt::testImplausibleSettingsFallBackToDefaults() {
  // A damaged or hand-edited settings file must not give empty thumbnails or mark every page as deviating.
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  settings.setThumbnailQuality(QSize());
  QCOMPARE(settings.getThumbnailQuality(), QSize(200, 200));
  settings.setThumbnailQuality(QSize(300, 0));
  QCOMPARE(settings.getThumbnailQuality(), QSize(200, 200));
  settings.setMaxLogicalThumbnailSize(QSizeF(0, 0));
  QCOMPARE(settings.getMaxLogicalThumbnailSize(), QSizeF(250, 160));

  settings.setDeskewDeviationCoef(0);
  QCOMPARE(settings.getDeskewDeviationCoef(), 1.5);
  settings.setSelectContentDeviationCoef(-1);
  QCOMPARE(settings.getSelectContentDeviationCoef(), 0.35);
  settings.setMarginsDeviationCoef(0);
  QCOMPARE(settings.getMarginsDeviationCoef(), 0.35);
  settings.setDeskewDeviationThreshold(-2);
  QCOMPARE(settings.getDeskewDeviationThreshold(), 1.0);
  settings.setSelectContentDeviationThreshold(-2);
  QCOMPARE(settings.getSelectContentDeviationThreshold(), 1.0);
  settings.setMarginsDeviationThreshold(-2);
  QCOMPARE(settings.getMarginsDeviationThreshold(), 1.0);

  // Plausible values are kept.
  settings.setThumbnailQuality(QSize(400, 400));
  QCOMPARE(settings.getThumbnailQuality(), QSize(400, 400));
  settings.setDeskewDeviationCoef(2.0);
  QCOMPARE(settings.getDeskewDeviationCoef(), 2.0);
  settings.setDeskewDeviationThreshold(0.0);
  QCOMPARE(settings.getDeskewDeviationThreshold(), 0.0);

  // Leave the settings as the other tests expect them.
  settings.setThumbnailQuality(QSize(200, 200));
  settings.setMaxLogicalThumbnailSize(QSizeF(250, 160));
  settings.setDeskewDeviationCoef(1.5);
  settings.setSelectContentDeviationCoef(0.35);
  settings.setMarginsDeviationCoef(0.35);
  settings.setDeskewDeviationThreshold(1.0);
  settings.setSelectContentDeviationThreshold(1.0);
  settings.setMarginsDeviationThreshold(1.0);
}

// A split line that misses the page has no inscribed part, a line from (0,0) to (0,0). The thumbnail
// must not draw it, QPainter would draw it as a dot at the top left of the image.
void TestCoreQt::testPageSplitThumbnailSkipsLineOutsideThePage() {
  const ImageTransformation xform(QRectF(0, 0, 100, 200), Dpi(300, 300));
  const page_split::PageLayout layout(QRectF(10, 10, 80, 180), QLineF(500, 0, 500, 200));
  QCOMPARE(layout.type(), page_split::PageLayout::TWO_PAGES);
  QVERIFY(layout.inscribedCutterLine(0).isNull());

  page_split::Thumbnail thumbnail(nullptr, QSizeF(100, 200), ImageId("page.png"), xform, layout, false, false);
  QImage image(40, 40, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    // The image origin lands at (5, 5), the page outline starts at (15, 15).
    thumbnail.prePaintOverImage(painter, QTransform::fromTranslate(5, 5), QTransform());
  }
  for (int y = 0; y < 12; ++y) {
    for (int x = 0; x < 12; ++x) {
      QVERIFY2(image.pixel(x, y) == qRgb(255, 255, 255), qPrintable(QString("pixel %1,%2 is drawn").arg(x).arg(y)));
    }
  }
}
