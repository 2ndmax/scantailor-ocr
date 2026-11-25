// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_deskew_qt.h"

#include "filter_ui_checks.h"

#include <DefaultParams.h>
#include <DefaultParamsProvider.h>
#include <FilterUiInterface.h>
#include <IncompleteThumbnail.h>
#include <PageId.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <filters/deskew/DeskewParams.h>
#include <filters/deskew/Settings.h>
#include <filters/deskew/UiData.h>
#include <filters/deskew/ui/ApplyDialog.h>
#include <filters/deskew/ui/ImageView.h>
#include <filters/deskew/ui/OptionsWidget.h>
#include <filters/deskew/ui/Thumbnail.h>

#include <QAbstractButton>
#include <QFrame>
#include <QImage>
#include <QPolygonF>
#include <QtTest/QtTest>
#include <memory>
#include <optional>
#include <set>
#include <typeinfo>

using namespace deskew;

namespace {
Dependencies measuredDeps() {
  QPolygonF outline;
  outline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100);
  return Dependencies(outline, OrthogonalRotation());
}

UiData uiData(const AutoManualMode mode, const AutoManualMode obliqueMode) {
  UiData data;
  data.setEffectiveDeskewAngle(0.5);
  data.setEffectiveObliqueAngle(1.5);
  data.setDependencies(measuredDeps());
  data.setMode(mode);
  data.setObliqueMode(obliqueMode);
  return data;
}

QAbstractButton* button(const OptionsWidget& widget, const char* name) {
  return widget.findChild<QAbstractButton*>(name);
}
}  // namespace

void TestDeskewQt::testObliqueAutoMeasuresAgain() {
  // Switching oblique to Auto must make the task measure the page, also with a manual deskew angle.
  auto settings = std::make_shared<Settings>();
  const PageId page(ImageId("page.png"));
  auto manualStart = std::make_shared<deskew::DeskewParams>();
  manualStart->setAutoOblique(false);
  const bool profileAutoOblique = DefaultParamsProvider::getInstance().getParams().getDeskewParams().isAutoOblique();
  OptionsWidget widget(settings, manualStart, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(page);
  widget.postUpdateUI(uiData(MODE_MANUAL, MODE_MANUAL));

  QAbstractButton* obliqueAutoBtn = button(widget, "obliqueAutoBtn");
  QVERIFY(obliqueAutoBtn != nullptr);
  obliqueAutoBtn->click();

  const std::unique_ptr<Params> params(settings->getPageParams(page));
  QVERIFY(params != nullptr);
  QCOMPARE(params->obliqueMode(), MODE_AUTO);
  QCOMPARE(params->mode(), MODE_MANUAL);
  QVERIFY(params->needsRemeasure(measuredDeps()));
  QCOMPARE(settings->takePendingAutoOblique(page), std::optional<bool>(true));
  // The choice applies to this page only: neither the start value of the project nor the one
  // of the profile changes.
  QVERIFY(!manualStart->isAutoOblique());
  QCOMPARE(DefaultParamsProvider::getInstance().getParams().getDeskewParams().isAutoOblique(), profileAutoOblique);
}

void TestDeskewQt::testObliqueManualLeavesNothingPending() {
  // Without a measurement following, nothing may stay pending for a later one.
  auto settings = std::make_shared<Settings>();
  const PageId page(ImageId("page.png"));
  OptionsWidget widget(settings, std::make_shared<deskew::DeskewParams>(), PageSelectionAccessor(nullptr));
  widget.preUpdateUI(page);
  widget.postUpdateUI(uiData(MODE_AUTO, MODE_AUTO));

  QAbstractButton* obliqueManualBtn = button(widget, "obliqueManualBtn");
  QVERIFY(obliqueManualBtn != nullptr);
  obliqueManualBtn->click();

  QCOMPARE(settings->takePendingAutoOblique(page), std::optional<bool>());
}

void TestDeskewQt::testApplyAutoObliqueMeasuresOtherPages() {
  // An automatic oblique angle is measured on each target page instead of being copied.
  auto settings = std::make_shared<Settings>();
  const PageId current(ImageId("current.png"));
  const PageId other(ImageId("other.png"));
  settings->setPageParams(other, Params(0.3, 0.7, measuredDeps(), MODE_MANUAL, MODE_MANUAL));
  settings->setPendingAutoOblique(other, false);

  OptionsWidget widget(settings, std::make_shared<deskew::DeskewParams>(), PageSelectionAccessor(nullptr));
  widget.preUpdateUI(current);
  widget.postUpdateUI(uiData(MODE_MANUAL, MODE_AUTO));

  QVERIFY(QMetaObject::invokeMethod(&widget, "appliedTo", Qt::DirectConnection,
                                    Q_ARG(std::set<PageId>, std::set<PageId>{other}), Q_ARG(bool, false),
                                    Q_ARG(bool, true)));

  const std::unique_ptr<Params> params(settings->getPageParams(other));
  QVERIFY(params != nullptr);
  QCOMPARE(params->deskewAngle(), 0.3);
  QCOMPARE(params->mode(), MODE_MANUAL);
  QCOMPARE(params->obliqueMode(), MODE_AUTO);
  QVERIFY(params->needsRemeasure(measuredDeps()));
  QCOMPARE(settings->takePendingAutoOblique(other), std::optional<bool>());
}

void TestDeskewQt::testApplyButtonWaitsForPage() {
  // While the next page is loading, the angles shown are still those of the previous one.
  auto settings = std::make_shared<Settings>();
  OptionsWidget widget(settings, std::make_shared<deskew::DeskewParams>(), PageSelectionAccessor(nullptr));
  QAbstractButton* applyBtn = button(widget, "applyDeskewBtn");
  QVERIFY(applyBtn != nullptr);

  widget.preUpdateUI(PageId(ImageId("page.png")));
  QVERIFY(!applyBtn->isEnabled());
  widget.postUpdateUI(uiData(MODE_AUTO, MODE_AUTO));
  QVERIFY(applyBtn->isEnabled());
}

void TestDeskewQt::testApplyDialogNeedsAnOption() {
  filter_ui_checks::checkApplyDialogNeedsAnOption<ApplyDialog>("applyDeskewCheckBox", "applyObliqueCheckBox");
}

void TestDeskewQt::testShowsPanelAndConnectedView() {
  // Showing a page puts the step's options panel and its image view on screen, connected to each
  // other, and updates the page's thumbnail.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->deskewFilterIdx();
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
  QVERIFY(QObject::disconnect(view, &ImageView::manualDeskewAngleSet, panel,
                              &OptionsWidget::manualDeskewAngleSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::manualDeskewAngleSet, view,
                              &ImageView::manualDeskewAngleSetExternally));
  QVERIFY(QObject::disconnect(view, &ImageView::manualObliqueAngleSet, panel,
                              &OptionsWidget::manualObliqueAngleSetExternally));
  QVERIFY(QObject::disconnect(panel, &OptionsWidget::manualObliqueAngleSet, view,
                              &ImageView::manualObliqueAngleSetExternally));
}

void TestDeskewQt::testThumbnailOfPage() {
  // Before the step has processed the page, its thumbnail says the page is incomplete; afterwards
  // it is the step's own.
  filter_ui_checks::OnePageProject one;
  QVERIFY(one.isValid());
  const PageInfo page = one.page(PAGE_VIEW);
  const int filterIdx = one.project.stages->deskewFilterIdx();
  const std::unique_ptr<QGraphicsItem> before = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(before != nullptr);
  QVERIFY(typeid(*before) == typeid(IncompleteThumbnail));

  filter_ui_checks::RecordingUi ui;
  QVERIFY(filter_ui_checks::showPage(one, page, filterIdx, ui));
  const std::unique_ptr<QGraphicsItem> after = filter_ui_checks::thumbnailFor(one, page, filterIdx);
  QVERIFY(after != nullptr);
  QVERIFY(typeid(*after) == typeid(Thumbnail));
}

void TestDeskewQt::testHandlesStayCenteredAtHiDpi() {
  QImage page(200, 200, QImage::Format_RGB32);
  page.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const ImageTransformation xform(QRectF(0, 0, 200, 200), Dpi(300, 300));

  ImageView view(page, page, xform);
  view.setFrameShape(QFrame::NoFrame);
  view.resize(400, 400);
  view.show();
  QVERIFY(QTest::qWaitForWindowExposed(&view));
  QCoreApplication::processEvents();
  const QImage shot = view.grab().toImage();
  const qreal dpr = shot.devicePixelRatio();

  qreal horY = 0.0;
  int horCount = 0;
  const int horYStart = qRound(190 * dpr);
  const int horYEnd = qMin(shot.height(), qRound(210 * dpr));
  const int horXStart = qRound(120 * dpr);
  const int horXEnd = qMin(shot.width(), qRound(170 * dpr));
  for (int y = horYStart; y < horYEnd; ++y) {
    for (int x = horXStart; x < horXEnd; ++x) {
      const QRgb px = shot.pixel(x, y);
      if (qBlue(px) > qRed(px) + 80) {
        horY += y / dpr;
        ++horCount;
      }
    }
  }

  qreal vertX = 0.0;
  int vertCount = 0;
  const int vertYStart = qRound(120 * dpr);
  const int vertYEnd = qMin(shot.height(), qRound(170 * dpr));
  const int vertXStart = qRound(190 * dpr);
  const int vertXEnd = qMin(shot.width(), qRound(210 * dpr));
  for (int y = vertYStart; y < vertYEnd; ++y) {
    for (int x = vertXStart; x < vertXEnd; ++x) {
      const QRgb px = shot.pixel(x, y);
      if (qBlue(px) > qRed(px) + 80) {
        vertX += x / dpr;
        ++vertCount;
      }
    }
  }

  QVERIFY(horCount > 0 && vertCount > 0);
  const qreal crosshairY = horY / horCount;
  const qreal crosshairX = vertX / vertCount;

  int upperHandleBlue = 0;
  int lowerHandleBlue = 0;
  for (int y = 0; y < shot.height(); ++y) {
    const qreal ly = y / dpr;
    const qreal dy = ly - crosshairY;
    if (qAbs(dy) <= 3.5 || qAbs(dy) >= 16.0) {
      continue;
    }
    for (int x = 0; x < shot.width(); ++x) {
      const qreal lx = x / dpr;
      if (qAbs(lx - crosshairX) <= 4.0) {
        continue;
      }
      const QRgb px = shot.pixel(x, y);
      if (qBlue(px) > qRed(px) + 80) {
        if (dy < 0.0) {
          ++upperHandleBlue;
        } else {
          ++lowerHandleBlue;
        }
      }
    }
  }

  QVERIFY(upperHandleBlue > 0 && lowerHandleBlue > 0);
  QVERIFY2(qAbs(upperHandleBlue - lowerHandleBlue) <= qMax(20, (upperHandleBlue + lowerHandleBlue) / 10),
           qPrintable(QString("upperHandleBlue=%1, lowerHandleBlue=%2, crosshairX=%3, crosshairY=%4, dpr=%5")
                          .arg(upperHandleBlue)
                          .arg(lowerHandleBlue)
                          .arg(crosshairX)
                          .arg(crosshairY)
                          .arg(dpr)));
}
