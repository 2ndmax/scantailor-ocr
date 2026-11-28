// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_output_qt.h"

#include <DefaultParams.h>
#include <ErrorWidget.h>
#include <FilterUiInterface.h>
#include <ImagePresentation.h>
#include <ImageTransformation.h>
#include <IncompleteThumbnail.h>
#include <PageSelectionAccessor.h>
#include <PageSelectionProvider.h>
#include <PageSequence.h>
#include <ProcessingOptions.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/OutputImageParams.h>
#include <filters/output/OutputParams.h>
#include <filters/output/Settings.h>
#include <filters/output/ui/ApplyParamsDialog.h>
#include <filters/output/ui/DespeckleView.h>
#include <filters/output/ui/DewarpingView.h>
#include <filters/output/ui/FillZoneEditor.h>
#include <filters/output/ui/ImageView.h>
#include <filters/output/ui/OptionsWidget.h>
#include <filters/output/ui/OptionsWidgetBinarizationLocal.h>
#include <filters/output/ui/OptionsWidgetBinarizationOtsu.h>
#include <filters/output/ui/PictureZoneEditor.h>
#include <filters/output/ui/TabbedImageView.h>
#include <filters/output/ui/Thumbnail.h>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QShortcut>
#include <QStackedWidget>
#include <QtTest/QtTest>
#include <cmath>
#include <memory>
#include <typeinfo>

#include "filter_ui_checks.h"

using namespace output;

namespace {
// The tabs in the order the step adds them.
enum Tab { OUTPUT_TAB, PICTURE_ZONES_TAB, FILL_ZONES_TAB, DEWARPING_TAB, DESPECKLING_TAB, TAB_COUNT };

DefaultParams mixedModeStartValues() {
  DefaultParams startValues;
  DefaultParams::OutputParams outputParams = startValues.getOutputParams();
  ColorParams colorParams = outputParams.getColorParams();
  colorParams.setColorMode(MIXED);
  outputParams.setColorParams(colorParams);
  startValues.setOutputParams(outputParams);
  return startValues;
}
}  // namespace

void TestOutputQt::testShowsPanelAndConnectedTabs() {
  // Showing a page puts the step's options panel and its tabs on screen, connected to each other,
  // and updates the page's thumbnail. In the default black and white mode, the picture zones tab
  // says it needs Mixed mode.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->outputFilterIdx();
  filter_ui_checks::RecordingUi ui;
  const FilterResultPtr result = filter_ui_checks::showPage(one, page, filterIdx, ui);
  QVERIFY(result);
  QVERIFY(result->filter() == one.project.stages->filterAt(filterIdx));

  auto* const panel = dynamic_cast<OptionsWidget*>(ui.optionsWidget);
  QVERIFY(panel != nullptr);
  QCOMPARE(ui.optionsWidgetOwnership, FilterUiInterface::KEEP_OWNERSHIP);
  QCOMPARE(ui.invalidatedThumbnails.size(), size_t(1));
  QCOMPARE(ui.invalidatedThumbnails[0], page.id());

  auto* const tabs = dynamic_cast<TabbedImageView*>(ui.imageWidget.get());
  QVERIFY(tabs != nullptr);
  QCOMPARE(tabs->count(), int(TAB_COUNT));
  QVERIFY(dynamic_cast<ImageView*>(tabs->widget(OUTPUT_TAB)) != nullptr);
  QVERIFY(dynamic_cast<ErrorWidget*>(tabs->widget(PICTURE_ZONES_TAB)) != nullptr);
  auto* const fillZones = dynamic_cast<FillZoneEditor*>(tabs->widget(FILL_ZONES_TAB));
  QVERIFY(fillZones != nullptr);
  auto* const dewarping = dynamic_cast<DewarpingView*>(tabs->widget(DEWARPING_TAB));
  QVERIFY(dewarping != nullptr);
  auto* const despeckling = dynamic_cast<DespeckleView*>(tabs->widget(DESPECKLING_TAB));
  QVERIFY(despeckling != nullptr);

  // disconnect() only succeeds for a connection that exists.
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::depthPerceptionChanged, dewarping,
                              &DewarpingView::depthPerceptionChanged));
  QVERIFY(QObject::disconnect(dewarping, &DewarpingView::distortionModelChanged, panel,
                              &OptionsWidget::distortionModelChanged));
  QVERIFY(QObject::disconnect(fillZones, &FillZoneEditor::invalidateThumbnail, panel,
                              qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail)));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::despeckleLevelChanged, despeckling,
                              &DespeckleView::despeckleLevelChanged));
  QVERIFY(QObject::disconnect(tabs, &TabbedImageView::tabChanged, panel, &OptionsWidget::tabChanged));
}

void TestOutputQt::testMixedModeShowsPictureZones() {
  // In Mixed mode, the picture zones tab is the zone editor, connected to the panel.
  filter_ui_checks::OnePageProject one(mixedModeStartValues());
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, one.project.stages->outputFilterIdx(), ui));

  auto* const panel = dynamic_cast<OptionsWidget*>(ui.optionsWidget);
  QVERIFY(panel != nullptr);
  auto* const tabs = dynamic_cast<TabbedImageView*>(ui.imageWidget.get());
  QVERIFY(tabs != nullptr);
  auto* const pictureZones = dynamic_cast<PictureZoneEditor*>(tabs->widget(PICTURE_ZONES_TAB));
  QVERIFY(pictureZones != nullptr);
  QVERIFY(QObject::disconnect(pictureZones, &PictureZoneEditor::invalidateThumbnail, panel,
                              qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail)));
}

void TestOutputQt::testThumbnailOfPage() {
  // Before the step has processed the page, its thumbnail says the page is incomplete; afterwards
  // it is the step's own, showing the output file.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->outputFilterIdx();
  const std::unique_ptr<QGraphicsItem> before = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(before != nullptr);
  QVERIFY(typeid(*before) == typeid(IncompleteThumbnail));

  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, filterIdx, ui));
  const std::unique_ptr<QGraphicsItem> after = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(after != nullptr);
  QVERIFY(typeid(*after) == typeid(Thumbnail));
}

void TestOutputQt::testTabbedImageViewEmptyRectAndZeroScrollBar() {
  // P8-10: Empty rects or zero-length scrollbars in TabbedImageView must not divide by 0 / produce NaN.
  TabbedImageView tabs;
  QScrollBar horBar(Qt::Horizontal);
  QScrollBar verBar(Qt::Vertical);
  horBar.setRange(0, 0);
  horBar.setPageStep(0);
  verBar.setRange(0, 0);
  verBar.setPageStep(0);

  const QPointF zeroBarFocus = tabs.getFocus(QRectF(10.0, 20.0, 100.0, 200.0), horBar, verBar);
  QVERIFY(std::isfinite(zeroBarFocus.x()));
  QVERIFY(std::isfinite(zeroBarFocus.y()));
  const QPointF emptyRectFocus = tabs.getFocus(QRectF(), horBar, verBar);
  QVERIFY(std::isfinite(emptyRectFocus.x()));
  QVERIFY(std::isfinite(emptyRectFocus.y()));
  tabs.setFocus(horBar, verBar, QRectF(), QPointF(5.0, 5.0));
  tabs.setFocus(horBar, verBar, QRectF(0.0, 0.0, 100.0, 100.0), QPointF(50.0, 50.0));

  QImage img(40, 40, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  auto* viewA = new ImageViewBase(img, ImagePixmapUnion(), ImagePresentation(QTransform(), QRectF(0, 0, 40, 40)));
  auto* viewB = new ImageViewBase(img, ImagePixmapUnion(), ImagePresentation(QTransform(), QRectF(0, 0, 40, 40)));
  tabs.addTab(viewA, QStringLiteral("A"), TAB_OUTPUT, QString());
  tabs.addTab(viewB, QStringLiteral("B"), TAB_DESPECKLING, QString());
  auto rectMap = std::make_unique<std::unordered_map<ImageViewTab, QRectF, std::hash<int>>>();
  (*rectMap)[TAB_OUTPUT] = QRectF();
  (*rectMap)[TAB_DESPECKLING] = QRectF(0, 0, 40, 40);
  tabs.setImageRectMap(std::move(rectMap));

  viewA->setZoomLevel(2.0);
  tabs.setCurrentIndex(0);
  tabs.setCurrentIndex(1);
  QCOMPARE(viewB->zoomLevel(), 1.0);
}

void TestOutputQt::testDewarpingViewDeleteKeyEmitsChangeAndNullptrSplineSafe() {
  // P8-12: initNewSpline(..., nullptr) and painting with invalid model / empty virtDisplayArea must not crash.
  XSpline nullOptSpline;
  DewarpingView::initNewSpline(nullOptSpline, QPointF(0.0, 0.0), QPointF(100.0, 0.0), nullptr);
  QCOMPARE(nullOptSpline.numControlPoints(), 2);

  QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  const PageId page(ImageId(QStringLiteral("dewarp.png")));
  {
    DewarpingView emptyView(img, ImagePixmapUnion(), QTransform(), QPolygonF(), QRectF(0, 0, 100, 100), page,
                            DewarpingOptions(MANUAL), dewarping::DistortionModel(), DepthPerception(2.0));
    emptyView.resize(120, 120);
    QPixmap target(120, 120);
    emptyView.render(&target);
    QVERIFY(emptyView.virtMarginArea(0).isEmpty());
  }

  // P8-11: Deleting a control point via 'D' must emit distortionModelChanged so Settings are updated.
  DewarpingOptions autoOpts(AUTO);
  XSpline topSpline;
  DewarpingView::initNewSpline(topSpline, QPointF(10.0, 20.0), QPointF(90.0, 20.0), &autoOpts);
  XSpline bottomSpline;
  DewarpingView::initNewSpline(bottomSpline, QPointF(10.0, 80.0), QPointF(90.0, 80.0), &autoOpts);
  dewarping::DistortionModel model;
  model.setTopCurve(dewarping::Curve(topSpline));
  model.setBottomCurve(dewarping::Curve(bottomSpline));

  DewarpingView view(img, ImagePixmapUnion(), QTransform(), QPolygonF(QRectF(0, 0, 100, 100)), QRectF(0, 0, 100, 100),
                     page, autoOpts, model, DepthPerception(2.0));
  view.resize(400, 400);
  view.show();
  QCoreApplication::processEvents();

  int changedCount = 0;
  QObject::connect(&view, &DewarpingView::distortionModelChanged,
                   [&changedCount](const dewarping::DistortionModel&) { ++changedCount; });

  const int pointsBefore = view.m_topSpline.spline().numControlPoints();
  const QPointF pt = view.sourceToWidget(view.m_topSpline.spline().controlPointPosition(1));
  QMouseEvent move(QEvent::MouseMove, pt, view.viewport()->mapToGlobal(pt.toPoint()), Qt::NoButton, Qt::NoButton,
                   Qt::NoModifier);
  move.ignore();
  QCoreApplication::sendEvent(view.viewport(), &move);

  emit view.m_removeControlPointShortcut->activated();
  QCOMPARE(view.m_topSpline.spline().numControlPoints(), pointsBefore - 1);
  QCOMPARE(changedCount, 1);
}

void TestOutputQt::testBinarizationUpdateUiStopsDelayedTimer() {
  // P8-13: Switching pages within 750 ms must cancel the delayed state-change timer in binarization subwidgets.
  auto settings = std::make_shared<Settings>();
  const PageId page1(ImageId(QStringLiteral("p1.png")));
  const PageId page2(ImageId(QStringLiteral("p2.png")));

  {
    OptionsWidgetBinarizationOtsu otsu(settings);
    int stateChangedCount = 0;
    QObject::connect(&otsu, &OptionsWidgetBinarization::stateChanged, [&stateChangedCount]() { ++stateChangedCount; });
    otsu.updateUi(page1);
    auto* deltaSpin = otsu.findChild<QDoubleSpinBox*>(QStringLiteral("otsuDelta"));
    QVERIFY(deltaSpin != nullptr);
    deltaSpin->setValue(12.0);
    otsu.updateUi(page2);
    QTest::qWait(850);
    QCOMPARE(stateChangedCount, 0);
  }

  {
    OptionsWidgetBinarizationLocal local(settings, T_SAUVOLA);
    int stateChangedCount = 0;
    QObject::connect(&local, &OptionsWidgetBinarization::stateChanged, [&stateChangedCount]() { ++stateChangedCount; });
    local.updateUi(page1);
    auto* deltaSpin = local.findChild<QDoubleSpinBox*>(QStringLiteral("delta"));
    QVERIFY(deltaSpin != nullptr);
    deltaSpin->setValue(8.0);
    local.updateUi(page2);
    QTest::qWait(850);
    QCOMPARE(stateChangedCount, 0);
  }
}

void TestOutputQt::testOptionsWidgetSignalsNullIndexImmediateSwitchAndPostDeskew() {
  // P8-14, P8-15, P8-16, P8-17: Dummy cleanup, -1 index safety, immediate method panel switch,
  // blocked updateColorsDisplay / cancelled delayed reload, and AUTO -> MANUAL post-deskew reload.
  auto settings = std::make_shared<Settings>();
  auto procOpts = std::make_shared<ProcessingOptions>();
  const PageId page(ImageId(QStringLiteral("opts.png")));
  OptionsWidget widget(settings, procOpts, PageSelectionAccessor(nullptr), std::make_shared<LastImageViewTab>());
  widget.preUpdateUI(page);

  auto* methodBox = widget.findChild<QComboBox*>(QStringLiteral("thresholdMethodBox"));
  auto* methodPanels = widget.findChild<QStackedWidget*>(QStringLiteral("binarizationOptions"));
  QVERIFY(methodBox != nullptr);
  QVERIFY(methodPanels != nullptr);
  QCOMPARE(methodPanels->count(), int(BlackWhiteOptions::METHOD_COUNT));

  // P8-15: updateBinarizationOptionsDisplay(-1) must not crash.
  widget.updateBinarizationOptionsDisplay(-1);

  int reloadCount = 0;
  QObject::connect(&widget, &FilterOptionsWidget::reloadRequested, [&reloadCount]() { ++reloadCount; });

  // P8-14 & P8-15: Immediate reload stops any pending delayed reload, and thresholdMethodChanged switches panel right
  // away.
  auto* dimming = widget.findChild<QDoubleSpinBox*>(QStringLiteral("coloredDimmingSB"));
  QVERIFY(dimming != nullptr);
  dimming->setValue(0.5);
  QVERIFY(widget.m_delayedReloadRequest.isActive());

  const int wolfIdx = methodBox->findData(int(T_WOLF));
  QVERIFY(wolfIdx >= 0);
  methodBox->setCurrentIndex(wolfIdx);
  QCOMPARE(static_cast<OptionsWidgetBinarization*>(methodPanels->currentWidget())->method(), T_WOLF);
  QCOMPARE(reloadCount, 1);
  QVERIFY(!widget.m_delayedReloadRequest.isActive());

  // P8-14: ColorParams with non-default chromaGrayLimit/wienerWindowSize updated during tabChanged must not trigger
  // extra reloads.
  ColorParams customColor = settings->getParams(page).colorParams();
  ColorCommonOptions common = customColor.colorCommonOptions();
  common.setChromaGrayLimit(12);
  common.setWienerCoef(0.4);
  common.setWienerWindowSize(7);
  customColor.setColorCommonOptions(common);
  settings->setColorParams(page, customColor);
  widget.preUpdateUI(page);
  reloadCount = 0;
  widget.tabChanged(TAB_OUTPUT);
  QVERIFY(!widget.m_delayedReloadRequest.isActive());
  QCOMPARE(reloadCount, 0);

  // P8-16: Switching AUTO -> MANUAL with active needPostDeskew and matching curves must reload.
  DewarpingOptions savedDewarp(AUTO, true);
  DewarpingOptions currentDewarp(MANUAL, true);
  dewarping::DistortionModel model;
  model.setTopCurve(dewarping::Curve(std::vector<QPointF>{QPointF(0, 10), QPointF(100, 10)}));
  model.setBottomCurve(dewarping::Curve(std::vector<QPointF>{QPointF(0, 90), QPointF(100, 90)}));

  settings->setDewarpingOptions(page, currentDewarp);
  settings->setDistortionModel(page, model);
  const ImageTransformation xform(QRectF(0, 0, 100, 100), Dpi(300, 300));
  OutputImageParams savedImgParams(QSize(100, 100), QRect(0, 0, 100, 100), xform, Dpi(300, 300), customColor,
                                   SplittingOptions(), savedDewarp, model, DepthPerception(2.0), 1.0,
                                   PictureShapeOptions(), OutputProcessingParams(), false);
  settings->setOutputParams(
      page, OutputParams(savedImgParams, OutputFileParams(), OutputFileParams(), OutputFileParams(), OutputFileParams(),
                         OutputFileParams(), OutputFileParams(), OutputFileParams(), ZoneSet(), ZoneSet()));
  widget.preUpdateUI(page);
  reloadCount = 0;
  widget.reloadIfNecessary();
  QCOMPARE(reloadCount, 1);
}

void TestOutputQt::testApplyParamsDialogModePartsAndThisPageIncluded() {
  struct StubProvider : public PageSelectionProvider {
    PageSequence pages;
    std::set<PageId> selected;
    explicit StubProvider(PageSequence seq) : pages(std::move(seq)) {}
    PageSequence allPages() const override { return pages; }
    std::set<PageId> selectedPages() const override { return selected; }
  };

  std::vector<ImageInfo> infos;
  for (int i = 0; i < 3; ++i) {
    infos.emplace_back(ImageId(QStringLiteral("out_%1.png").arg(i)), ImageMetadata(QSize(200, 300), Dpi(300, 300)), 1,
                       false, false);
  }
  const auto projectPages = std::make_shared<ProjectPages>(infos, Qt::LeftToRight);
  const PageSequence seq = projectPages->toPageSequence(PAGE_VIEW);
  auto provider = std::make_shared<StubProvider>(seq);

  // Mode parts + disabling both checkboxes disables OK button.
  ApplyParamsDialog modeDialog(nullptr, seq.pageAt(0).id(), PageSelectionAccessor(provider));
  modeDialog.showModeParts();
  auto* box = modeDialog.findChild<QDialogButtonBox*>();
  auto* colorModeOpt = modeDialog.findChild<QCheckBox*>(QStringLiteral("applyColorModeOption"));
  auto* otherOpt = modeDialog.findChild<QCheckBox*>(QStringLiteral("applyOtherSettingsOption"));
  QVERIFY(box != nullptr);
  QVERIFY(colorModeOpt != nullptr);
  QVERIFY(otherOpt != nullptr);

  colorModeOpt->setChecked(false);
  QVERIFY(box->button(QDialogButtonBox::Ok)->isEnabled());
  QVERIFY(!modeDialog.isColorModeChosen());
  QVERIFY(modeDialog.isOtherSettingsChosen());
  otherOpt->setChecked(false);
  QVERIFY(!box->button(QDialogButtonBox::Ok)->isEnabled());
  colorModeOpt->setChecked(true);
  QVERIFY(box->button(QDialogButtonBox::Ok)->isEnabled());

  // setThisPageIncluded(true) emits {curPage} on THIS_PAGE; default (false) emits empty set.
  std::set<PageId> emitted;
  ApplyParamsDialog thisIncludedDialog(nullptr, seq.pageAt(1).id(), PageSelectionAccessor(provider));
  thisIncludedDialog.setThisPageIncluded(true);
  QObject::connect(&thisIncludedDialog, &ApplyParamsDialog::accepted,
                   [&emitted](const std::set<PageId>& p) { emitted = p; });
  thisIncludedDialog.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
  QCOMPARE(emitted, std::set<PageId>{seq.pageAt(1).id()});
}
