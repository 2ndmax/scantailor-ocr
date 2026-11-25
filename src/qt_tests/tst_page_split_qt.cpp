// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_split_qt.h"

#include <FilterUiInterface.h>
#include <IncompleteThumbnail.h>
#include <PageSequence.h>
#include <filters/page_split/ui/ImageView.h>
#include <filters/page_split/ui/OptionsWidget.h>
#include <filters/page_split/ui/Thumbnail.h>

#include <QFrame>
#include <QImage>
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

void TestPageSplitQt::testHandlesStayCenteredAtHiDpi() {
  QImage page(200, 200, QImage::Format_RGB32);
  page.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const ImageTransformation xform(QRectF(0, 0, 200, 200), Dpi(300, 300));
  const PageLayout layout(QRectF(0, 0, 200, 200), QLineF(100, 0, 100, 200));

  ImageView view(page, page, xform, layout, nullptr, ImageId("page.png"), false, false);
  view.setFrameShape(QFrame::NoFrame);
  view.resize(400, 400);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QCoreApplication::processEvents();
  const QImage shot = view.grab().toImage();
  const qreal dpr = shot.devicePixelRatio();

  qreal lineSumX = 0.0;
  int lineCount = 0;
  const int lineYStart = qRound(150 * dpr);
  const int lineYEnd = qMin(shot.height(), qRound(250 * dpr));
  for (int y = lineYStart; y < lineYEnd; ++y) {
    for (int x = 0; x < shot.width(); ++x) {
      const QRgb px = shot.pixel(x, y);
      if (qBlue(px) > qRed(px) + 80) {
        lineSumX += x / dpr;
        ++lineCount;
      }
    }
  }

  qreal handleSumX = 0.0;
  int handleCount = 0;
  const int handleYEnd = qMin(shot.height(), qRound(30 * dpr));
  for (int y = 0; y < handleYEnd; ++y) {
    for (int x = 0; x < shot.width(); ++x) {
      const QRgb px = shot.pixel(x, y);
      if (qBlue(px) > qRed(px) + 80) {
        handleSumX += x / dpr;
        ++handleCount;
      }
    }
  }

  QVERIFY(lineCount > 0 && handleCount > 0);
  const qreal lineAvgX = lineSumX / lineCount;
  const qreal handleAvgX = handleSumX / handleCount;
  QVERIFY2(qAbs(handleAvgX - lineAvgX) <= 1.5,
           qPrintable(QString("handleAvgX=%1, lineAvgX=%2, dpr=%3").arg(handleAvgX).arg(lineAvgX).arg(dpr)));
}
