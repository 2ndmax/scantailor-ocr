// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_split_qt.h"

#include <FilterUiInterface.h>
#include <IncompleteThumbnail.h>
#include <PageSequence.h>
#include <filters/page_split/ImageView.h>
#include <filters/page_split/OptionsWidget.h>
#include <filters/page_split/Thumbnail.h>

#include <QtTest/QtTest>
#include <memory>
#include <typeinfo>

#include "filter_ui_checks.h"

using namespace page_split;

void TestPageSplitQt::testShowsPanelAndConnectedView() {
  // Showing a page puts the step's options panel and its image view on screen, connected to each
  // other, and updates the page's thumbnail.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(IMAGE_VIEW);
  const int filterIdx = one.project.stages->pageSplitFilterIdx();
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
  QVERIFY(QObject::disconnect(view, &ImageView::invalidateThumbnail, panel,
                              qOverload<const PageInfo&>(&OptionsWidget::invalidateThumbnail)));
  QVERIFY(QObject::disconnect(view, &ImageView::pageLayoutSetLocally, panel, &OptionsWidget::pageLayoutSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::pageLayoutSetLocally, view, &ImageView::pageLayoutSetExternally));
}

void TestPageSplitQt::testThumbnailOfPage() {
  // Before the step has processed the page, its thumbnail says the page is incomplete; afterwards
  // it is the step's own.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(IMAGE_VIEW);
  const int filterIdx = one.project.stages->pageSplitFilterIdx();
  const std::unique_ptr<QGraphicsItem> before = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(before != nullptr);
  QVERIFY(typeid(*before) == typeid(IncompleteThumbnail));

  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, filterIdx, ui));
  const std::unique_ptr<QGraphicsItem> after = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(after != nullptr);
  QVERIFY(typeid(*after) == typeid(Thumbnail));
}
