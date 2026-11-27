// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_split_qt.h"

#include <FilterUiInterface.h>
#include <IncompleteThumbnail.h>
#include <PageSequence.h>
#include <core/gui/IconProvider.h>
#include <core/gui/OverlayStyle.h>
#include <core/gui/StyledIconPack.h>
#include <filters/page_split/Dependencies.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/Params.h>
#include <filters/page_split/Settings.h>
#include <filters/page_split/UiData.h>
#include <filters/page_split/ui/ImageView.h>
#include <filters/page_split/ui/OptionsWidget.h>
#include <filters/page_split/ui/Thumbnail.h>

#include <QDomDocument>
#include <QFrame>
#include <QImage>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QToolButton>
#include <QtTest/QtTest>
#include <cmath>
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

void TestPageSplitQt::testReloadsWhenSwitchingFromUncutWithMissingCutter() {
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(IMAGE_VIEW);

  auto settings = std::make_shared<Settings>();
  OptionsWidget panel(settings, one.project.pages, PageSelectionAccessor(nullptr));

  const QRectF outline(0, 0, 200, 300);
  PageLayout uncutAfterXml;
  {
    PageLayout layout(outline, QLineF(100, 0, 100, 300));
    layout.setType(PageLayout::SINGLE_PAGE_UNCUT);
    const Dependencies deps(QSize(200, 300), OrthogonalRotation(), QPolygonF(outline), SINGLE_PAGE_UNCUT);
    const Params params(layout, deps, MODE_MANUAL);
    QDomDocument doc;
    const Params reloaded(params.toXml(doc, "params"));
    uncutAfterXml = reloaded.pageLayout();
  }
  QVERIFY(uncutAfterXml.cutterLine(0).isNull());

  UiData uiData;
  uiData.setPageLayout(uncutAfterXml);
  uiData.setDependencies(Dependencies(QSize(200, 300), OrthogonalRotation(), QPolygonF(outline), SINGLE_PAGE_UNCUT));
  uiData.setSplitLineMode(MODE_MANUAL);
  uiData.setLayoutTypeAutoDetected(false);

  panel.preUpdateUI(page.id());
  panel.postUpdateUI(uiData);

  QSignalSpy reloadSpy(&panel, &OptionsWidget::reloadRequested);
  auto* const twoPagesBtn = panel.findChild<QToolButton*>("twoPagesBtn");
  QVERIFY(twoPagesBtn != nullptr);
  twoPagesBtn->click();
  QCOMPARE(reloadSpy.count(), 1);
  const Settings::Record reloadedRecord = settings->getPageRecord(page.id().imageId());
  QVERIFY(!reloadedRecord.params());

  // Local toggle from TWO_PAGES -> SINGLE_PAGE_UNCUT with valid cutter updates stored dependencies too.
  UiData twoPagesUiData;
  twoPagesUiData.setPageLayout(PageLayout(outline, QLineF(100, 0, 100, 300)));
  twoPagesUiData.setDependencies(Dependencies(QSize(200, 300), OrthogonalRotation(), QPolygonF(outline), TWO_PAGES));
  twoPagesUiData.setSplitLineMode(MODE_MANUAL);
  twoPagesUiData.setLayoutTypeAutoDetected(false);
  panel.preUpdateUI(page.id());
  panel.postUpdateUI(twoPagesUiData);

  reloadSpy.clear();
  auto* const uncutBtn = panel.findChild<QToolButton*>("singlePageUncutBtn");
  QVERIFY(uncutBtn != nullptr);
  uncutBtn->click();
  QCOMPARE(reloadSpy.count(), 0);
  const Settings::Record localRecord = settings->getPageRecord(page.id().imageId());
  QVERIFY(localRecord.params());
  QDomDocument depsDoc;
  const QDomElement depsEl = localRecord.params()->dependencies().toXml(depsDoc, "deps");
  QCOMPARE(depsEl.namedItem("layoutType").toElement().text(), layoutTypeToString(SINGLE_PAGE_UNCUT));
}

void TestPageSplitQt::testCutterHandleDragWhileZoomedDoesNotInvertLimits() {
  QImage page(200, 400, QImage::Format_RGB32);
  page.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const ImageTransformation xform(QRectF(0, 0, 200, 400), Dpi(300, 300));
  const PageLayout layout(QRectF(0, 0, 200, 400), QLineF(10, 0, 160, 400), QLineF(40, 0, 180, 400));

  ImageView view(page, page, xform, layout, nullptr, ImageId("page.png"), false, false);
  view.setFrameShape(QFrame::NoFrame);
  view.resize(400, 400);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  view.setZoomLevel(4.0);
  view.setWidgetFocalPoint(view.getWidgetFocalPoint() + QPointF(250.0, 0.0));
  QCoreApplication::processEvents();

  const QRectF occ = view.getOccupiedWidgetRect();
  const qreal delta = 0.5 * OverlayStyle::handlePixmap(view.devicePixelRatioF()).deviceIndependentSize().height();
  const QLineF wCutter1 = view.virtualToWidget().map(layout.cutterLine(1));
  QPointF topHandle;
  QVERIFY(wCutter1.intersects(QLineF(occ.left(), occ.top() + delta, occ.right(), occ.top() + delta), &topHandle)
          != QLineF::NoIntersection);

  int layoutSignals = 0;
  PageLayout committed;
  QObject::connect(&view, &ImageView::pageLayoutSetLocally, [&](const PageLayout& layoutResult) {
    ++layoutSignals;
    committed = layoutResult;
  });
  InteractionState state;
  view.rootInteractionHandler().proximityUpdate(topHandle, state);
  QMouseEvent press(QEvent::MouseButtonPress, topHandle, topHandle, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  press.ignore();
  view.rootInteractionHandler().mousePressEvent(&press, state);
  QVERIFY(state.captured());

  const QPointF dragged = topHandle + QPointF(8.0, 0.0);
  QMouseEvent move(QEvent::MouseMove, dragged, dragged, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  move.ignore();
  view.rootInteractionHandler().mouseMoveEvent(&move, state);
  QMouseEvent release(QEvent::MouseButtonRelease, dragged, dragged, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  release.ignore();
  view.rootInteractionHandler().mouseReleaseEvent(&release, state);

  QCOMPARE(layoutSignals, 1);
  const QLineF cutter1 = committed.cutterLine(1);
  QVERIFY(std::isfinite(cutter1.x1()) && std::isfinite(cutter1.x2()));
}

void TestPageSplitQt::testUnremoveButtonDisabledOutsideTwoPages() {
  struct ScopedIconPack {
    ScopedIconPack() { IconProvider::getInstance().setIconPack(StyledIconPack::createDefault()); }
    ~ScopedIconPack() { IconProvider::getInstance().setIconPack(nullptr); }
  } scopedIconPack;

  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(IMAGE_VIEW);

  QImage img(200, 200, QImage::Format_RGB32);
  img.fill(Qt::white);
  const ImageTransformation xform(QRectF(0, 0, 200, 200), Dpi(300, 300));
  const PageLayout twoPagesLayout(QRectF(0, 0, 200, 200), QLineF(100, 0, 100, 200));

  one.project.pages->setLayoutTypeFor(page.imageId(), ProjectPages::TWO_PAGE_LAYOUT);
  one.project.pages->removePages({PageId(page.imageId(), PageId::LEFT_PAGE)});
  ImageView view(img, img, xform, twoPagesLayout, one.project.pages, page.imageId(), true, false);
  view.resize(400, 400);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));

  int thumbCount = 0;
  QObject::connect(&view, &ImageView::invalidateThumbnail, [&](const PageInfo&) { ++thumbCount; });

  PageLayout uncutLayout(twoPagesLayout);
  uncutLayout.setType(PageLayout::SINGLE_PAGE_UNCUT);
  view.pageLayoutSetExternally(uncutLayout);

  InteractionState state;
  view.rootInteractionHandler().proximityUpdate(QPointF(0.0, 0.0), state);
  QMouseEvent pressAtOrigin(QEvent::MouseButtonPress, QPointF(0.0, 0.0), QPointF(0.0, 0.0), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
  pressAtOrigin.ignore();
  view.rootInteractionHandler().mousePressEvent(&pressAtOrigin, state);
  QCOMPARE(thumbCount, 0);
  QVERIFY(!pressAtOrigin.isAccepted());

  // Returning to TWO_PAGES restores the unremove button at the removed left page center.
  view.pageLayoutSetExternally(twoPagesLayout);
  const QPointF leftCenter = view.virtualToWidget().map(QPointF(50.0, 100.0));
  InteractionState twoPageState;
  view.rootInteractionHandler().proximityUpdate(leftCenter, twoPageState);
  QMouseEvent pressOnButton(QEvent::MouseButtonPress, leftCenter, leftCenter, Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
  pressOnButton.ignore();
  view.rootInteractionHandler().mousePressEvent(&pressOnButton, twoPageState);
  QCOMPARE(thumbCount, 1);
}
