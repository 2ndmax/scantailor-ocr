// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_output_qt.h"

#include <DefaultParams.h>
#include <ErrorWidget.h>
#include <FilterUiInterface.h>
#include <IncompleteThumbnail.h>
#include <PageSequence.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DespeckleView.h>
#include <filters/output/DewarpingView.h>
#include <filters/output/FillZoneEditor.h>
#include <filters/output/ImageView.h>
#include <filters/output/OptionsWidget.h>
#include <filters/output/PictureZoneEditor.h>
#include <filters/output/TabbedImageView.h>
#include <filters/output/Thumbnail.h>

#include <QtTest/QtTest>
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
