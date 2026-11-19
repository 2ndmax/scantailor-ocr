// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_layout_qt.h"

#include "filter_ui_checks.h"

#include <FilterUiInterface.h>
#include <ImageInfo.h>
#include <IncompleteThumbnail.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <filters/page_layout/Settings.h>
#include <filters/page_layout/ui/ImageView.h>
#include <filters/page_layout/ui/OptionsWidget.h>
#include <filters/page_layout/ui/Thumbnail.h>

#include <QtTest/QtTest>
#include <memory>
#include <typeinfo>
#include <vector>

#include "length_field_checks.h"

using namespace page_layout;

void TestPageLayoutQt::testLengthFieldsSetDecimalsStepAndUnit() {
  const struct {
    Units units;
    int decimals;
    double step;
  } formats[] = {{PIXELS, 0, 1.0}, {MILLIMETRES, 1, 1.0}, {CENTIMETRES, 2, 0.01}, {INCHES, 2, 0.01}};
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>(), Qt::LeftToRight);
  page_layout::OptionsWidget widget(std::make_shared<page_layout::Settings>(), pages, PageSelectionAccessor(nullptr));
  for (const auto& format : formats) {
    const ScopedUnits scopedUnits(format.units);
    for (const char* name : {"topMarginSpinBox", "bottomMarginSpinBox", "leftMarginSpinBox", "rightMarginSpinBox",
                             "targetWidthSpinBox", "targetHeightSpinBox"}) {
      QVERIFY2(lengthFieldShows(widget.findChild<QDoubleSpinBox*>(name), format.units, format.decimals, format.step),
               name);
    }
  }
}

void TestPageLayoutQt::testShowsPanelAndConnectedView() {
  // Showing a page puts the step's options panel and its image view on screen, connected to each
  // other, and updates the page's thumbnail.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->pageLayoutFilterIdx();
  filter_ui_checks::RecordingUi ui;
  const FilterResultPtr result = filter_ui_checks::showPage(one, page, filterIdx, ui);
  QVERIFY(result);
  QVERIFY(result->filter() == one.project.stages->filterAt(filterIdx));

  auto* const panel = dynamic_cast<OptionsWidget*>(ui.optionsWidget);
  QVERIFY(panel != nullptr);
  QCOMPARE(ui.optionsWidgetOwnership, FilterUiInterface::KEEP_OWNERSHIP);
  auto* const view = dynamic_cast<ImageView*>(ui.imageWidget.get());
  QVERIFY(view != nullptr);
  // The first page sets the size all pages share, so all thumbnails are updated, or just its own.
  QCOMPARE(ui.invalidatedThumbnails.size() + size_t(ui.allThumbnailsInvalidated), size_t(1));
  // disconnect() only succeeds for a connection that exists.
  QVERIFY(QObject::disconnect(view, &ImageView::invalidateThumbnail, panel,
                              qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail)));
  QVERIFY(
      QObject::disconnect(view, &ImageView::invalidateAllThumbnails, panel, &OptionsWidget::invalidateAllThumbnails));
  QVERIFY(QObject::disconnect(view, &ImageView::marginsSetLocally, panel, &OptionsWidget::marginsSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::marginsSetLocally, view, &ImageView::marginsSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::topBottomLinkToggled, view, &ImageView::topBottomLinkToggled));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::leftRightLinkToggled, view, &ImageView::leftRightLinkToggled));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::alignmentChanged, view, &ImageView::alignmentChanged));
  QVERIFY(
      QObject::disconnect(panel, &OptionsWidget::aggregateHardSizeChanged, view, &ImageView::aggregateHardSizeChanged));
}

void TestPageLayoutQt::testThumbnailOfPage() {
  // Before the step has processed the page, its thumbnail says the page is incomplete; afterwards
  // it is the step's own.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->pageLayoutFilterIdx();
  const std::unique_ptr<QGraphicsItem> before = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(before != nullptr);
  QVERIFY(typeid(*before) == typeid(IncompleteThumbnail));

  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, filterIdx, ui));
  const std::unique_ptr<QGraphicsItem> after = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(after != nullptr);
  QVERIFY(typeid(*after) == typeid(Thumbnail));
}
