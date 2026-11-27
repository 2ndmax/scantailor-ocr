// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_layout_qt.h"

#include "filter_ui_checks.h"

#include <ContentMask.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterUiInterface.h>
#include <ImageInfo.h>
#include <ImageMetadata.h>
#include <ImageTransformation.h>
#include <IncompleteThumbnail.h>
#include <NullTaskStatus.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <filters/page_layout/Alignment.h>
#include <filters/page_layout/Guide.h>
#include <filters/page_layout/OutputSizeOptions.h>
#include <filters/page_layout/Params.h>
#include <filters/page_layout/Settings.h>
#include <filters/page_layout/ui/ImageView.h>
#include <filters/page_layout/ui/OptionsWidget.h>
#include <filters/page_layout/ui/Thumbnail.h>

#include <imageproc/GrayImage.h>
#include <QDoubleSpinBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QToolButton>
#include <QtTest/QtTest>
#include <cmath>
#include <memory>
#include <set>
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

void TestPageLayoutQt::testOptionsWidgetAlignmentButtonApplyMarginsAndCommonSizeGroup() {
  qRegisterMetaType<std::set<PageId>>("std::set<PageId>");
  auto settings = std::make_shared<Settings>();
  const PageId pageA(ImageId("page_a.png"));
  const PageId pageB(ImageId("page_b.png"));
  const ImageInfo infoA(pageA.imageId(), ImageMetadata(QSize(1000, 1500), Dpi(300, 300)), 1, false, false);
  const ImageInfo infoB(pageB.imageId(), ImageMetadata(QSize(1000, 1500), Dpi(300, 300)), 1, false, false);
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{infoA, infoB}, Qt::LeftToRight);
  const PageInfo pageInfoA(pageA, infoA.metadata, 1, false, false);
  const PageInfo pageInfoB(pageB, infoB.metadata, 1, false, false);

  OptionsWidget widget(settings, pages, PageSelectionAccessor(nullptr));

  auto* const commonSizeGroup = widget.findChild<QWidget*>("commonSizeGroup");
  QVERIFY(commonSizeGroup != nullptr);

  widget.preUpdateUI(pageInfoA, Margins(), Alignment(Alignment::TOP, Alignment::LEFT));
  QVERIFY(!commonSizeGroup->isEnabled());
  widget.postUpdateUI();
  QVERIFY(commonSizeGroup->isEnabled());

  auto* const bottomRightBtn = widget.findChild<QToolButton*>("alignBottomRightBtn");
  auto* const topRightBtn = widget.findChild<QToolButton*>("alignTopRightBtn");
  auto* const centerBtn = widget.findChild<QToolButton*>("alignCenterBtn");
  QVERIFY(bottomRightBtn && topRightBtn && centerBtn);

  Alignment scaledBottomRight(Alignment::BOTTOM, Alignment::RIGHT);
  scaledBottomRight.setScaleToFit(true);
  widget.preUpdateUI(pageInfoB, Margins(), scaledBottomRight);
  QVERIFY(bottomRightBtn->isChecked());
  widget.postUpdateUI();

  Alignment offTopRight(Alignment::TOP, Alignment::RIGHT);
  offTopRight.setNull(true);
  widget.preUpdateUI(pageInfoA, Margins(), offTopRight);
  QVERIFY(topRightBtn->isChecked());
  widget.postUpdateUI();

  Alignment autoBoth(Alignment::VAUTO, Alignment::HAUTO);
  widget.preUpdateUI(pageInfoA, Margins(), autoBoth);
  QVERIFY(centerBtn->isChecked());
  widget.postUpdateUI();

  // P7-12: applyMargins with auto margins skips m_pageId so pageA stays valid while pageB is invalidated.
  settings->setContentSizeMM(pageA, QSizeF(80.0, 120.0));
  settings->setContentSizeMM(pageB, QSizeF(80.0, 120.0));
  settings->setPageAutoMarginsEnabled(pageA, true);
  widget.preUpdateUI(pageInfoA, Margins(), Alignment(Alignment::VCENTER, Alignment::HCENTER));
  widget.postUpdateUI();

  const std::set<PageId> targets{pageA, pageB};
  QVERIFY(QMetaObject::invokeMethod(&widget, "applyMargins", Qt::DirectConnection, Q_ARG(std::set<PageId>, targets),
                                    Q_ARG(bool, true), Q_ARG(bool, true), Q_ARG(bool, true), Q_ARG(bool, true)));
  const std::unique_ptr<Params> paramsA = settings->getPageParams(pageA);
  const std::unique_ptr<Params> paramsB = settings->getPageParams(pageB);
  QVERIFY(paramsA && paramsA->contentSizeMM().isValid());
  QVERIFY(paramsB && !paramsB->contentSizeMM().isValid());
  QVERIFY(settings->isPageAutoMarginsEnabled(pageB));
}

void TestPageLayoutQt::testGuidesRespectScaleAndDoNotShrinkSettings() {
  const PageId pageId(ImageId("scaled.png"));
  QImage largeImg(1000, 1500, QImage::Format_RGB32);
  largeImg.fill(Qt::white);
  const Dpm dpm1000(Dpi(254, 254));
  largeImg.setDotsPerMeterX(dpm1000.horizontal());
  largeImg.setDotsPerMeterY(dpm1000.vertical());
  ImageTransformation scaledXform(QRectF(0, 0, 1000, 1500), Dpi(254, 254));

  auto settings = std::make_shared<Settings>();
  OutputSizeOptions sizeOpts;
  sizeOpts.setOwnTargetSize(true);
  sizeOpts.setTargetSizeMM(QSizeF(200.0, 300.0));
  sizeOpts.setMaxEnlargePercent(200);
  settings->setOutputSizeOptions(sizeOpts);

  const ImageInfo largeInfo(pageId.imageId(), ImageMetadata(QSize(1000, 1500), Dpi(254, 254)), 1, false, false);
  const PageInfo largePage(pageId, largeInfo.metadata, 1, false, false);
  auto largePages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{largeInfo}, Qt::LeftToRight);

  Alignment scaledCenter(Alignment::VCENTER, Alignment::HCENTER);
  scaledCenter.setScaleToFit(true);
  settings->setHardMarginsMM(pageId, Margins(0, 0, 0, 0));
  settings->setPageAlignment(pageId, scaledCenter);
  settings->setContentSizeMM(pageId, QSizeF(100.0, 150.0));
  settings->updateContentSizeAndGetParams(pageId, QRectF(0, 0, 1000, 1500), QRectF(0, 0, 1000, 1500),
                                          QSizeF(100.0, 150.0));
  settings->guides().emplace_back(Qt::Vertical, 90.0);

  OptionsWidget optWidget(settings, largePages, PageSelectionAccessor(nullptr));
  optWidget.preUpdateUI(largePage, Margins(0, 0, 0, 0), scaledCenter);
  optWidget.postUpdateUI();

  ContentMask mask(imageproc::GrayImage(largeImg), scaledXform, NullTaskStatus());
  ImageView view(settings, pageId, largeImg, largeImg, mask, scaledXform, QRectF(0, 0, 1000, 1500), optWidget);
  view.resize(400, 400);
  view.alignmentChanged(scaledCenter);
  view.aggregateHardSizeChanged();

  QCOMPARE(settings->guides().size(), size_t(1));
  QCOMPARE(settings->guides()[0].getPosition(), 90.0);

  // Snapping a scaled page verifies guidePosInVirtual divides by m_scaleFactor = 2.0 (1 mm output = 5 virtual px).
  settings->guides().clear();
  settings->guides().emplace_back(Qt::Vertical, -80.0);
  ImageTransformation snapXform(QRectF(0, 0, 1000, 1500), Dpi(254, 254));
  QImage blockImg(1000, 1500, QImage::Format_RGB32);
  blockImg.fill(Qt::white);
  for (int y = 500; y < 700; ++y) {
    for (int x = 300; x < 320; ++x) {
      blockImg.setPixel(x, y, qRgb(0, 0, 0));
    }
  }
  blockImg.setDotsPerMeterX(dpm1000.horizontal());
  blockImg.setDotsPerMeterY(dpm1000.vertical());
  ContentMask blockMask(imageproc::GrayImage(blockImg), snapXform, NullTaskStatus());
  settings->setContentSizeMM(pageId, QSizeF(30.0, 20.0));
  settings->updateContentSizeAndGetParams(pageId, QRectF(0, 0, 1000, 1500), QRectF(300, 500, 300, 200),
                                          QSizeF(30.0, 20.0));
  Alignment scaledLeft(Alignment::TOP, Alignment::LEFT);
  scaledLeft.setScaleToFit(true);
  optWidget.preUpdateUI(largePage, Margins(0, 0, 0, 0), scaledLeft);
  optWidget.postUpdateUI();
  ImageView scaledView(settings, pageId, blockImg, blockImg, blockMask, snapXform, QRectF(300, 500, 300, 200),
                       optWidget);
  scaledView.resize(400, 400);

  const QPointF snapClick = scaledView.virtualToWidget().map(QPointF(310.0, 600.0));
  InteractionState snapState;
  QMouseEvent snapDblClick(QEvent::MouseButtonDblClick, snapClick, snapClick, Qt::LeftButton, Qt::LeftButton,
                           Qt::NoModifier);
  snapDblClick.ignore();
  scaledView.rootInteractionHandler().mouseDoubleClickEvent(&snapDblClick, snapState);
  QVERIFY2(std::abs(settings->getHardMarginsMM(pageId).left() - 16.0) <= 1.5,
           qPrintable(
               QString("Expected scaled snap margin ~16.0 mm, got %1").arg(settings->getHardMarginsMM(pageId).left())));
}

void TestPageLayoutQt::testNegativeHardMarginsDoNotFlipPositive() {
  const PageId pageId(ImageId("neg_margins.png"));
  const ImageInfo info(pageId.imageId(), ImageMetadata(QSize(1000, 1500), Dpi(254, 254)), 1, false, false);
  const PageInfo pageInfo(pageId, info.metadata, 1, false, false);
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{info}, Qt::LeftToRight);

  auto settings = std::make_shared<Settings>();
  const Margins negMargins(-5.0, -3.0, -4.0, -2.0);
  const Alignment align(Alignment::VCENTER, Alignment::HCENTER);
  settings->setHardMarginsMM(pageId, negMargins);
  settings->setPageAlignment(pageId, align);
  settings->setContentSizeMM(pageId, QSizeF(50.0, 80.0));
  settings->updateContentSizeAndGetParams(pageId, QRectF(0, 0, 1000, 1500), QRectF(250, 350, 500, 800),
                                          QSizeF(50.0, 80.0));

  OptionsWidget optWidget(settings, pages, PageSelectionAccessor(nullptr));
  optWidget.preUpdateUI(pageInfo, negMargins, align);
  optWidget.postUpdateUI();

  QImage img(1000, 1500, QImage::Format_RGB32);
  img.fill(Qt::white);
  const ImageTransformation xform(QRectF(0, 0, 1000, 1500), Dpi(254, 254));
  ImageView view(settings, pageId, img, img, ContentMask(), xform, QRectF(250, 350, 500, 800), optWidget);
  view.resize(400, 400);

  view.alignmentChanged(Alignment(Alignment::TOP, Alignment::LEFT));
  view.leftRightLinkToggled(true);
  const Margins saved = settings->getHardMarginsMM(pageId);
  QVERIFY(saved.left() < 0.0);
  QCOMPARE(saved.left(), -5.0);
  QCOMPARE(saved.right(), -5.0);
}

void TestPageLayoutQt::testAttachContentToNearestGuideWithEdgeAlignment() {
  const PageId pageId(ImageId("guide_snap.png"));
  const ImageInfo info(pageId.imageId(), ImageMetadata(QSize(2000, 2000), Dpi(254, 254)), 1, false, false);
  const PageInfo pageInfo(pageId, info.metadata, 1, false, false);
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{info}, Qt::LeftToRight);

  auto settings = std::make_shared<Settings>();
  OutputSizeOptions sizeOpts;
  sizeOpts.setOwnTargetSize(true);
  sizeOpts.setTargetSizeMM(QSizeF(200.0, 200.0));
  settings->setOutputSizeOptions(sizeOpts);

  const Alignment topLeft(Alignment::TOP, Alignment::LEFT);
  settings->setHardMarginsMM(pageId, Margins(0, 0, 0, 0));
  settings->setPageAlignment(pageId, topLeft);
  settings->setContentSizeMM(pageId, QSizeF(40.0, 40.0));
  settings->updateContentSizeAndGetParams(pageId, QRectF(0, 0, 2000, 2000), QRectF(500, 500, 400, 400),
                                          QSizeF(40.0, 40.0));
  // Outer rect starts at inner.left = 500 (50 mm), width = 200 mm -> center at 150 mm.
  // Vertical guide at -80 mm sits at 70 mm (= 700 px, 20 mm to the right of content left = 500 px).
  settings->guides().emplace_back(Qt::Vertical, -80.0);

  OptionsWidget optWidget(settings, pages, PageSelectionAccessor(nullptr));
  optWidget.preUpdateUI(pageInfo, Margins(0, 0, 0, 0), topLeft);
  optWidget.postUpdateUI();

  QImage img(2000, 2000, QImage::Format_RGB32);
  img.fill(Qt::white);
  for (int y = 500; y < 900; ++y) {
    for (int x = 500; x < 520; ++x) {
      img.setPixel(x, y, qRgb(0, 0, 0));
    }
  }
  const Dpm dpm1000(Dpi(254, 254));
  img.setDotsPerMeterX(dpm1000.horizontal());
  img.setDotsPerMeterY(dpm1000.vertical());
  const ImageTransformation xform(QRectF(0, 0, 2000, 2000), Dpi(254, 254));
  const ContentMask mask(imageproc::GrayImage(img), xform, NullTaskStatus());

  ImageView view(settings, pageId, img, img, mask, xform, QRectF(500, 500, 400, 400), optWidget);
  view.resize(400, 400);

  const QPointF clickPt = view.virtualToWidget().map(QPointF(510.0, 700.0));
  InteractionState state;
  QMouseEvent dblClick(QEvent::MouseButtonDblClick, clickPt, clickPt, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  dblClick.ignore();
  view.rootInteractionHandler().mouseDoubleClickEvent(&dblClick, state);

  const Margins snapped = settings->getHardMarginsMM(pageId);
  QVERIFY2(std::abs(snapped.left() - 18.0) <= 1.5,
           qPrintable(QString("Expected left margin ~18.0 mm without edge overshoot, got %1").arg(snapped.left())));
}
