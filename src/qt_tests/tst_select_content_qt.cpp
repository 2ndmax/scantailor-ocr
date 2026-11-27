// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_select_content_qt.h"

#include "filter_ui_checks.h"

#include "length_field_checks.h"

#include <DefaultParams.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <FilterUiInterface.h>
#include <ImageMetadata.h>
#include <IncompleteThumbnail.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <filters/select_content/Dependencies.h>
#include <filters/select_content/DetectionOptions.h>
#include <filters/select_content/Params.h>
#include <filters/select_content/SelectContentParams.h>
#include <filters/select_content/Settings.h>
#include <filters/select_content/Task.h>
#include <filters/select_content/UiData.h>
#include <filters/select_content/ui/ApplyDialog.h>
#include <filters/select_content/ui/ImageView.h>
#include <filters/select_content/ui/OptionsWidget.h>
#include <filters/select_content/ui/Thumbnail.h>

#include <QMetaObject>
#include <QMouseEvent>
#include <QPolygonF>
#include <QSignalSpy>
#include <QSpinBox>
#include <QtTest/QtTest>
#include <memory>
#include <optional>
#include <set>
#include <typeinfo>

using namespace select_content;

namespace {
const PageId PAGE(ImageId("page.png"));

QPolygonF outline() {
  QPolygonF polygon;
  polygon << QPointF(0, 0) << QPointF(600, 0) << QPointF(600, 800) << QPointF(0, 800);
  return polygon;
}

/** A page whose content box was detected automatically with \p detectedWith. */
UiData detectedPage(const std::optional<DetectionOptions>& detectedWith) {
  UiData data;
  data.setContentRect(QRectF(50, 50, 500, 700));
  data.setPageRect(QRectF(0, 0, 600, 800));
  data.setContentDetectionMode(MODE_AUTO);
  data.setPageDetectionMode(MODE_DISABLED);
  data.setDetectionOptions(detectedWith);
  data.setDependencies(Dependencies(outline(), MODE_AUTO, MODE_DISABLED, false, detectedWith));
  return data;
}

/** Whether the task would detect the content box again, as it decides in Task::process(). */
bool contentBoxDetectedAgain(const Settings& settings) {
  const std::unique_ptr<Params> params(settings.getPageParams(PAGE));
  if (!params) {
    return false;
  }
  const Dependencies current(outline(), params->contentDetectionMode(), params->pageDetectionMode(),
                             params->isFineTuningEnabled(), params->detectionOptions());
  bool updateContentBox = false;
  bool updatePageBox = false;
  current.compatibleWith(params->dependencies(), &updateContentBox, &updatePageBox);
  return updateContentBox;
}

struct Panel {
  std::shared_ptr<Settings> settings = std::make_shared<Settings>();
  OptionsWidget widget{settings, std::make_shared<select_content::SelectContentParams>(),
                       PageSelectionAccessor(nullptr)};

  explicit Panel(const UiData& data) {
    widget.preUpdateUI(PageInfo(PAGE, ImageMetadata(QSize(600, 800), Dpi(300, 300)), 1, false, false));
    widget.postUpdateUI(data);
  }

  QSpinBox* fillFactor() const { return widget.findChild<QSpinBox*>("fillFactorSpinBox"); }
};
}  // namespace

void TestSelectContentQt::testChangedDetectionOptionsDetectAgain() {
  // Other settings than the box was detected with make the task detect it again; going back to
  // them does not, as the box already matches them.
  const DetectionOptions detectedWith(70, 0, false);
  Panel panel(detectedPage(detectedWith));
  QVERIFY(panel.fillFactor() != nullptr);

  panel.fillFactor()->setValue(80);
  QVERIFY(contentBoxDetectedAgain(*panel.settings));
  panel.fillFactor()->setValue(70);
  QVERIFY(!contentBoxDetectedAgain(*panel.settings));
}

void TestSelectContentQt::testSizeFieldsSetDecimalsStepAndUnit() {
  const struct {
    Units units;
    int decimals;
    double step;
  } formats[] = {{PIXELS, 0, 1.0}, {MILLIMETRES, 1, 1.0}, {CENTIMETRES, 2, 0.01}, {INCHES, 2, 0.01}};
  Panel panel{UiData()};
  for (const auto& format : formats) {
    const ScopedUnits scopedUnits(format.units);
    for (const char* name : {"widthSpinBox", "heightSpinBox"}) {
      QVERIFY2(
          lengthFieldShows(panel.widget.findChild<QDoubleSpinBox*>(name), format.units, format.decimals, format.step),
          name);
    }
  }
}

void TestSelectContentQt::testOldPagesDetectAgainOnlyWithOtherOptions() {
  // Pages detected before the settings existed have none stored; they were detected with the
  // defaults.
  Panel panel(detectedPage(std::nullopt));
  QVERIFY(panel.fillFactor() != nullptr);
  QCOMPARE(panel.fillFactor()->value(), DetectionOptions::DEFAULT_FILL_FACTOR);

  panel.fillFactor()->setValue(80);
  QVERIFY(contentBoxDetectedAgain(*panel.settings));
  panel.fillFactor()->setValue(DetectionOptions::DEFAULT_FILL_FACTOR);
  QVERIFY(!contentBoxDetectedAgain(*panel.settings));
}

void TestSelectContentQt::testApplyDialogNeedsAnOption() {
  filter_ui_checks::checkApplyDialogNeedsAnOption<ApplyDialog>("applyContentBoxOption", "applyPageBoxOption");
}

void TestSelectContentQt::testShowsPanelAndConnectedView() {
  // Showing a page puts the step's options panel and its image view on screen, connected to each
  // other, and updates the page's thumbnail.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->selectContentFilterIdx();
  filter_ui_checks::RecordingUi ui;
  const FilterResultPtr result = filter_ui_checks::showPage(one, page, filterIdx, ui);
  QVERIFY(result);
  QVERIFY(result->filter() == one.project.stages->filterAt(filterIdx));

  auto* const panel = dynamic_cast<OptionsWidget*>(ui.optionsWidget);
  QVERIFY(panel != nullptr);
  QCOMPARE(ui.optionsWidgetOwnership, FilterUiInterface::KEEP_OWNERSHIP);
  auto* const view = dynamic_cast<ImageView*>(ui.imageWidget.get());
  QVERIFY(view != nullptr);
  QCOMPARE(ui.invalidatedThumbnails.size(), size_t(1));
  QCOMPARE(ui.invalidatedThumbnails[0], page.id());
  // disconnect() only succeeds for a connection that exists.
  QVERIFY(QObject::disconnect(view, &ImageView::manualContentRectSet, panel, &OptionsWidget::manualContentRectSet));
  QVERIFY(QObject::disconnect(view, &ImageView::manualPageRectSet, panel, &OptionsWidget::manualPageRectSet));
  QVERIFY(QObject::disconnect(view, &ImageView::pageRectSizeChanged, panel, &OptionsWidget::updatePageRectSize));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::pageRectChangedLocally, view, &ImageView::pageRectSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::pageRectStateChanged, view, &ImageView::setPageRectEnabled));
}

void TestSelectContentQt::testThumbnailOfPage() {
  // Before the step has processed the page, its thumbnail says the page is incomplete; afterwards
  // it is the step's own.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->selectContentFilterIdx();
  const std::unique_ptr<QGraphicsItem> before = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(before != nullptr);
  QVERIFY(typeid(*before) == typeid(IncompleteThumbnail));

  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, filterIdx, ui));
  const std::unique_ptr<QGraphicsItem> after = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(after != nullptr);
  QVERIFY(typeid(*after) == typeid(Thumbnail));
}

void TestSelectContentQt::testApplySelectionPreservesManualBoxes() {
  qRegisterMetaType<std::set<PageId>>("std::set<PageId>");
  const PageId pageB(ImageId("page_b.png"));
  const QPolygonF rectOutline(outline());

  // P7-02: Eccentric manual page box copied onto processed target keeps its position after Task::process,
  // and target's manual content box is not shifted by stale delta.
  {
    UiData sourceData;
    sourceData.setPageRect(QRectF(20, 40, 200, 300));
    sourceData.setContentRect(QRectF(30, 50, 150, 200));
    sourceData.setPageDetectionMode(MODE_MANUAL);
    sourceData.setContentDetectionMode(MODE_AUTO);
    sourceData.setDependencies(Dependencies(rectOutline, MODE_AUTO, MODE_MANUAL, false));

    Panel panel(sourceData);
    panel.settings->setPageParams(
        pageB, Params(QRectF(100, 120, 300, 400), QSizeF(), QRectF(0, 0, 600, 800),
                      Dependencies(rectOutline, MODE_MANUAL, MODE_DISABLED, false), MODE_MANUAL, MODE_DISABLED, false));

    const std::set<PageId> targets{pageB};
    QVERIFY(QMetaObject::invokeMethod(&panel.widget, "applySelection", Qt::DirectConnection,
                                      Q_ARG(std::set<PageId>, targets), Q_ARG(bool, false), Q_ARG(bool, true)));

    QImage image(600, 800, QImage::Format_RGB32);
    image.fill(Qt::white);
    const Dpm dpm(Dpi(300, 300));
    image.setDotsPerMeterX(dpm.horizontal());
    image.setDotsPerMeterY(dpm.vertical());
    Task task(nullptr, nullptr, panel.settings, pageB, DetectionOptions(), true, false);
    task.process(NullTaskStatus(), FilterData(image));

    const std::unique_ptr<Params> updated = panel.settings->getPageParams(pageB);
    QVERIFY(updated != nullptr);
    QCOMPARE(updated->pageRect(), QRectF(20, 40, 200, 300));
    QCOMPARE(updated->contentRect(), QRectF(100, 120, 300, 400));
  }

  // P7-03: applyContentBox without applyPageBox does not shift manual content box when target pageRect is invalid.
  {
    UiData sourceData;
    sourceData.setPageRect(QRectF(50, 60, 500, 700));
    sourceData.setContentRect(QRectF(100, 120, 300, 400));
    sourceData.setPageDetectionMode(MODE_AUTO);
    sourceData.setContentDetectionMode(MODE_MANUAL);
    sourceData.setDependencies(Dependencies(rectOutline, MODE_MANUAL, MODE_AUTO, false));

    Panel panel(sourceData);
    panel.settings->setPageParams(
        pageB, Params(QRectF(), QSizeF(), QRectF(), Dependencies(rectOutline, MODE_AUTO, MODE_DISABLED, false),
                      MODE_AUTO, MODE_DISABLED, false));

    const std::set<PageId> targets{pageB};
    QVERIFY(QMetaObject::invokeMethod(&panel.widget, "applySelection", Qt::DirectConnection,
                                      Q_ARG(std::set<PageId>, targets), Q_ARG(bool, true), Q_ARG(bool, false)));

    const std::unique_ptr<Params> updated = panel.settings->getPageParams(pageB);
    QVERIFY(updated != nullptr);
    QCOMPARE(updated->contentRect(), QRectF(100, 120, 300, 400));
  }
}

void TestSelectContentQt::testEmptyContentRectAndPageBoxBounds() {
  QImage img(200, 300, QImage::Format_RGB32);
  img.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  img.setDotsPerMeterX(dpm.horizontal());
  img.setDotsPerMeterY(dpm.vertical());
  const ImageTransformation xform(QRectF(0, 0, 200, 300), Dpi(300, 300));

  // P7-07: Non-null empty contentRect (QRectF(100, 150, 0, 0)) does not attach active content box handlers.
  {
    ImageView view(img, img, ContentMask(), xform, QRectF(100, 150, 0, 0), QRectF(0, 0, 200, 300), true);
    view.resize(400, 400);

    QSignalSpy contentSpy(&view, &ImageView::manualContentRectSet);
    const QPointF emptyBoxPt = view.virtualToWidget().map(QPointF(100, 150));
    const QPointF movedPt = emptyBoxPt + QPointF(20.0, 20.0);
    InteractionState state;
    view.rootInteractionHandler().proximityUpdate(emptyBoxPt, state);
    QMouseEvent press(QEvent::MouseButtonPress, emptyBoxPt, emptyBoxPt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.ignore();
    view.rootInteractionHandler().mousePressEvent(&press, state);
    QMouseEvent move(QEvent::MouseMove, movedPt, movedPt, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move.ignore();
    view.rootInteractionHandler().mouseMoveEvent(&move, state);
    QMouseEvent rel(QEvent::MouseButtonRelease, movedPt, movedPt, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    rel.ignore();
    view.rootInteractionHandler().mouseReleaseEvent(&rel, state);
    QCOMPARE(contentSpy.count(), 0);

    QVERIFY(QMetaObject::invokeMethod(&view, "createContentBox", Qt::DirectConnection));
    QCOMPARE(contentSpy.count(), 1);
    const QRectF created = contentSpy.at(0).at(0).toRectF();
    QVERIFY(!created.isEmpty());
    QVERIFY(QRectF(0, 0, 200, 300).contains(created));
  }

  // P7-07: pageRectSetExternally and dragging page edge stay within virtualDisplayRect().
  {
    ImageView view(img, img, ContentMask(), xform, QRectF(), QRectF(20, 20, 150, 200), true);
    view.resize(400, 400);

    QSignalSpy pageSpy(&view, &ImageView::manualPageRectSet);
    view.pageRectSetExternally(QRectF(50, 50, 500, 600));
    QCOMPARE(pageSpy.count(), 1);
    const QRectF clampedExternal = pageSpy.at(0).at(0).toRectF();
    QVERIFY(QRectF(0, 0, 200, 300).contains(clampedExternal));

    pageSpy.clear();
    const QPointF rightEdge
        = view.virtualToWidget().map(QPointF(clampedExternal.right(), clampedExternal.center().y()));
    InteractionState state;
    view.rootInteractionHandler().proximityUpdate(rightEdge, state);
    QMouseEvent press(QEvent::MouseButtonPress, rightEdge, rightEdge, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press.ignore();
    view.rootInteractionHandler().mousePressEvent(&press, state);
    QVERIFY(state.captured());

    const QPointF farRight = view.virtualToWidget().map(QPointF(350.0, clampedExternal.center().y()));
    QMouseEvent move(QEvent::MouseMove, farRight, farRight, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move.ignore();
    view.rootInteractionHandler().mouseMoveEvent(&move, state);
    QMouseEvent release(QEvent::MouseButtonRelease, farRight, farRight, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    release.ignore();
    view.rootInteractionHandler().mouseReleaseEvent(&release, state);

    QCOMPARE(pageSpy.count(), 1);
    const QRectF clampedDragged = pageSpy.at(0).at(0).toRectF();
    QVERIFY(QRectF(0, 0, 200, 300).contains(clampedDragged));
  }
}
