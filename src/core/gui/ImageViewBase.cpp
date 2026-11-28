// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageViewBase.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QTextEdit>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <algorithm>

#include "ApplicationSettings.h"
#include "BackgroundExecutor.h"
#include "DownscaledImage.h"
#include "Dpm.h"
#include "ImagePresentation.h"
#include "ImageViewInfoListener.h"
#include "OpenGLSupport.h"
#include "PixmapRenderer.h"
#include "ScopedIncDec.h"
#include "WidgetUtils.h"
#include "ZoomHandler.h"

using namespace core;

namespace {
ImageViewCoordMapper::FocalPointMode toMapperMode(const ImageViewBase::FocalPointMode mode) {
  return (mode == ImageViewBase::CENTER_IF_FITS) ? ImageViewCoordMapper::FocalPointMode::CenterIfFits
                                                 : ImageViewCoordMapper::FocalPointMode::DontCenter;
}
}  // namespace

/**
 * \brief Temporarily adjust the widget focal point, then change it back.
 *
 * When adjusting and restoring the widget focal point, the pixmap
 * focal point is recalculated accordingly.
 */
class ImageViewBase::TempFocalPointAdjuster {
 public:
  /**
   * Change the widget focal point to obj's centered widget focal point.
   */
  explicit TempFocalPointAdjuster(ImageViewBase& obj);

  /**
   * Change the widget focal point to \p tempWidgetFp.
   */
  TempFocalPointAdjuster(ImageViewBase& obj, QPointF tempWidgetFp);

  /**
   * Restore the widget focal point.
   */
  ~TempFocalPointAdjuster();

 private:
  ImageViewBase& m_obj;
  QPointF m_origWidgetFP;
};


class ImageViewBase::TransformChangeWatcher {
 public:
  explicit TransformChangeWatcher(ImageViewBase& owner);

  ~TransformChangeWatcher();

 private:
  ImageViewBase& m_owner;
  QTransform m_imageToVirtual;
  QTransform m_virtualToWidget;
  QRectF m_virtualDisplayArea;
};


ImageViewBase::ImageViewBase(const QImage& image,
                             const ImagePixmapUnion& downscaledVersion,
                             const ImagePresentation& presentation,
                             const Margins& margins,
                             BackgroundExecutor* backgroundExecutor)
    : m_coordMapper(presentation, margins),
      m_hqCache(image,
                backgroundExecutor ? *backgroundExecutor : ImageViewBase::backgroundExecutor(),
                [this]() { return buildHqRenderRequest(); }),
      m_lastMaximumViewportSize(maximumViewportSize()),
      m_infoProvider(Dpm(m_hqCache.image())),
      m_transformChangeWatchersActive(0),
      m_ignoreScrollEvents(0),
      m_ignoreResizeEvents(0) {
  /* For some reason, the default viewport fills background with
   * a color different from QPalette::Window at the first show on Windows.
   * Here we make it not fill it automatically at all
   * doing the work in paintEvent().
   */
  setAttribute(Qt::WA_OpaquePaintEvent);

  /* For some reason, the default viewport fills background with
   * a color different from QPalette::Window. Here we make it not
   * fill it at all, assuming QMainWindow will do that anyway
   * (with the correct color). Note that an attempt to do the same
   * to an OpenGL viewport produces "black hole" artefacts. Therefore,
   * we do this before setting an OpenGL viewport rather than after.
   */
  viewport()->setAutoFillBackground(false);

  // REVIEW-NOTE (2026-10): every view gets its own QOpenGLWidget -> one OpenGL context per open view.
  // Not fixed because OpenGL is off by default and only one or two views are open at a time.
  if (ApplicationSettings::getInstance().isOpenGlEnabled()) {
    if (OpenGLSupport::supported()) {
      setViewport(new QOpenGLWidget());
    }
  }

  setFrameShape(QFrame::NoFrame);
  viewport()->setFocusPolicy(Qt::WheelFocus);

  if (downscaledVersion.isNull()) {
    if (!image.isNull()) {
      m_pixmap = QPixmap::fromImage(createDownscaledImage(image));
    }
  } else if (downscaledVersion.pixmap().isNull()) {
    m_pixmap = QPixmap::fromImage(downscaledVersion.image());
  } else {
    m_pixmap = downscaledVersion.pixmap();
  }

  m_coordMapper.setPixmapScale(m_hqCache.image().size(), m_pixmap.size());
  m_coordMapper.initializeFocalPoints(maximumViewportSize());

  connect(&m_hqCache, &HqImageCache::ready, this, &ImageViewBase::updateViewport);

  setMouseTracking(true);
  m_cursorTrackerTimer.setSingleShot(true);
  m_cursorTrackerTimer.setInterval(150);  // msec
  connect(&m_cursorTrackerTimer, &QTimer::timeout, this, [this]() {
    QPointF cursorPos;
    if (m_cursorPos.has_value()) {
      cursorPos = m_coordMapper.widgetToVirtual().map(*m_cursorPos)
                  - m_coordMapper.virtualImageCropArea().boundingRect().topLeft();
    }
    m_infoProvider.setMousePos(cursorPos);
  });

  // During construction a virtual call can only reach this class's version, so say so explicitly.
  // The one override (page_layout::ImageView) updates the size again at the end of its constructor,
  // via recalcBoxesAndFit().
  ImageViewBase::updatePhysSize();

  updateWidgetTransformAndFixFocalPoint(CENTER_IF_FITS);

  interactionState().setDefaultStatusTip(
      tr("Use the mouse wheel or +/- to zoom. When zoomed, dragging is possible. Double-click to see the whole page."));
  ensureStatusTip(interactionState().statusTip());

  connect(horizontalScrollBar(), &QAbstractSlider::sliderReleased, this, &ImageViewBase::updateScrollBars);
  connect(verticalScrollBar(), &QAbstractSlider::sliderReleased, this, &ImageViewBase::updateScrollBars);
  connect(horizontalScrollBar(), &QAbstractSlider::valueChanged, this, &ImageViewBase::reactToScrollBars);
  connect(verticalScrollBar(), &QAbstractSlider::valueChanged, this, &ImageViewBase::reactToScrollBars);
}

ImageViewBase::~ImageViewBase() = default;

QRectF ImageViewBase::maxViewportRect() const {
  return m_coordMapper.maxViewportRect(maximumViewportSize());
}

QRectF ImageViewBase::getOccupiedWidgetRect() const {
  return m_coordMapper.occupiedWidgetRect(QRectF(viewport()->rect()));
}

QImage ImageViewBase::sampleImageAtWidgetRect(const QRect& widgetRect, const int maxImageSide) const {
  return m_hqCache.sampleAtWidgetRect(widgetToImage(), widgetRect, maxImageSide);
}

void ImageViewBase::setWidgetFocalPoint(const QPointF& widgetFp) {
  const TransformChangeWatcher watcher(*this);
  if (m_coordMapper.setWidgetFocalPoint(widgetFp, maximumViewportSize())) {
    updateViewport();
  }
}

void ImageViewBase::adjustAndSetWidgetFocalPoint(const QPointF& widgetFp) {
  const TransformChangeWatcher watcher(*this);
  if (m_coordMapper.adjustAndSetWidgetFocalPoint(widgetFp, maximumViewportSize())) {
    updateViewport();
  }
}

void ImageViewBase::setWidgetFocalPointWithoutMoving(const QPointF newWidgetFp) {
  m_coordMapper.setWidgetFocalPointWithoutMoving(newWidgetFp);
}

void ImageViewBase::setZoomLevel(const double zoom) {
  const TransformChangeWatcher watcher(*this);
  if (m_coordMapper.setZoomLevel(zoom, maximumViewportSize())) {
    updateViewport();
  }
}

void ImageViewBase::moveTowardsIdealPosition(const double pixelLength) {
  const TransformChangeWatcher watcher(*this);
  if (m_coordMapper.moveTowardsIdealPosition(pixelLength, maximumViewportSize())) {
    updateViewport();
  }
}

void ImageViewBase::updateTransform(const ImagePresentation& presentation) {
  const TransformChangeWatcher watcher(*this);
  const TempFocalPointAdjuster tempFp(*this);

  m_coordMapper.setPresentation(presentation);

  updateWidgetTransform();
  updateViewport();
  updatePhysSize();
}

void ImageViewBase::updateTransformAndFixFocalPoint(const ImagePresentation& presentation, const FocalPointMode mode) {
  const TransformChangeWatcher watcher(*this);
  const TempFocalPointAdjuster tempFp(*this);

  m_coordMapper.setPresentation(presentation);

  updateWidgetTransformAndFixFocalPoint(mode);
  updateViewport();
  updatePhysSize();
}

void ImageViewBase::updateTransformPreservingScale(const ImagePresentation& presentation) {
  const TransformChangeWatcher watcher(*this);
  const TempFocalPointAdjuster tempFp(*this);

  m_coordMapper.updateTransformPreservingScale(presentation, maximumViewportSize());

  updateViewport();
  updatePhysSize();
}

void ImageViewBase::ensureStatusTip(const QString& statusTip) {
  const QString curStatusTip(viewport()->statusTip());
  if (curStatusTip.constData() == statusTip.constData()) {
    return;
  }
  if (curStatusTip == statusTip) {
    return;
  }

  viewport()->setStatusTip(statusTip);

  if (viewport()->underMouse()) {
    // Note that setStatusTip() alone is not enough,
    // as it's only taken into account when the mouse
    // enters the widget.
    // Also note that we use postEvent() rather than sendEvent(),
    // because sendEvent() may immediately process other events.
    QApplication::postEvent(viewport(), new QStatusTipEvent(statusTip));
  }
}

void ImageViewBase::registerZoomHandler(ZoomHandler* const handler) {
  if (!handler) {
    return;
  }
  auto it = std::find(m_zoomHandlers.begin(), m_zoomHandlers.end(), handler);
  if (it != m_zoomHandlers.end()) {
    m_zoomHandlers.erase(it);
  }
  m_zoomHandlers.push_back(handler);
  for (size_t i = 0; i < m_zoomHandlers.size(); ++i) {
    m_zoomHandlers[i]->setShortcutsEnabled(i + 1 == m_zoomHandlers.size());
  }
}

void ImageViewBase::unregisterZoomHandler(ZoomHandler* const handler) {
  auto it = std::find(m_zoomHandlers.begin(), m_zoomHandlers.end(), handler);
  if (it == m_zoomHandlers.end()) {
    return;
  }
  m_zoomHandlers.erase(it);
  for (size_t i = 0; i < m_zoomHandlers.size(); ++i) {
    m_zoomHandlers[i]->setShortcutsEnabled(i + 1 == m_zoomHandlers.size());
  }
}

void ImageViewBase::setInfoListener(ImageViewInfoListener* listener) {
  if (m_infoListener == listener) {
    return;
  }
  if (isVisible() && (m_infoListener != nullptr)) {
    infoProvider().removeListener(m_infoListener);
  }
  m_infoListener = listener;
  if (isVisible() && (m_infoListener != nullptr)) {
    infoProvider().addListener(m_infoListener);
  }
}

void ImageViewBase::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(viewport());

  // Fill the background as Qt::WA_OpaquePaintEvent attribute is enabled.
  painter.fillRect(viewport()->rect(), palette().color(backgroundRole()));

  painter.save();

  const QTransform& virtualToWidget = m_coordMapper.virtualToWidget();
  const double xscale = virtualToWidget.m11();

  // Width of a source pixel in mm, as it's displayed on screen.
  const int w = width();
  const double pixelWidth = (w > 0) ? (widthMM() * xscale / w) : 0.0;

  // Make clipping smooth.
  painter.setRenderHint(QPainter::Antialiasing, true);

  // Disable antialiasing for large zoom levels.
  painter.setRenderHint(QPainter::SmoothPixmapTransform, pixelWidth < 0.5);

  const HqImageCache::RenderRequest renderRequest = buildHqRenderRequest();
  if (m_hqCache.isValid(renderRequest)) {
    // HQ pixmap maps one to one to screen pixels, so antialiasing is not necessary.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    QPainterPath clipPath;
    clipPath.addPolygon(virtualToWidget.map(m_coordMapper.virtualImageCropArea()));
    painter.setClipPath(clipPath);

    painter.drawPixmap(m_hqCache.pixmapPos(), m_hqCache.pixmap());
  } else {
    m_hqCache.scheduleRebuild(renderRequest);

    const QTransform pixmapToVirtual(m_coordMapper.pixmapToImage() * m_coordMapper.imageToVirtual());
    painter.setWorldTransform(pixmapToVirtual * virtualToWidget);

    QPainterPath clipPath;
    clipPath.addPolygon(pixmapToVirtual.inverted().map(m_coordMapper.virtualImageCropArea()));
    painter.setClipPath(clipPath);

    PixmapRenderer::drawPixmap(painter, m_pixmap);
  }

  painter.restore();

  painter.setWorldTransform(virtualToWidget);

  m_interactionState.resetProximity();
  if (!m_interactionState.captured()) {
    m_rootInteractionHandler.proximityUpdate(QPointF(0.5, 0.5) + viewport()->mapFromGlobal(QCursor::pos()),
                                             m_interactionState);
    updateStatusTipAndCursor();
  }

  m_rootInteractionHandler.paint(painter, m_interactionState);
  maybeQueueRedraw();
}  // ImageViewBase::paintEvent

void ImageViewBase::keyPressEvent(QKeyEvent* event) {
  event->setAccepted(false);
  m_rootInteractionHandler.keyPressEvent(event, m_interactionState);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::keyReleaseEvent(QKeyEvent* event) {
  event->setAccepted(false);
  m_rootInteractionHandler.keyReleaseEvent(event, m_interactionState);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::mousePressEvent(QMouseEvent* event) {
  m_interactionState.resetProximity();
  if (!m_interactionState.captured()) {
    m_rootInteractionHandler.proximityUpdate(QPointF(0.5, 0.5) + event->pos(), m_interactionState);
  }

  event->setAccepted(false);
  m_rootInteractionHandler.mousePressEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::mouseReleaseEvent(QMouseEvent* event) {
  m_interactionState.resetProximity();
  if (!m_interactionState.captured()) {
    m_rootInteractionHandler.proximityUpdate(QPointF(0.5, 0.5) + event->pos(), m_interactionState);
  }

  event->setAccepted(false);
  m_rootInteractionHandler.mouseReleaseEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::mouseDoubleClickEvent(QMouseEvent* event) {
  m_interactionState.resetProximity();
  if (!m_interactionState.captured()) {
    m_rootInteractionHandler.proximityUpdate(QPointF(0.5, 0.5) + event->pos(), m_interactionState);
  }

  event->setAccepted(false);
  m_rootInteractionHandler.mouseDoubleClickEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::mouseMoveEvent(QMouseEvent* event) {
  m_interactionState.resetProximity();
  if (!m_interactionState.captured()) {
    m_rootInteractionHandler.proximityUpdate(QPointF(0.5, 0.5) + event->pos(), m_interactionState);
  }

  event->setAccepted(false);
  m_rootInteractionHandler.mouseMoveEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
  updateCursorPos(event->position());
}

void ImageViewBase::wheelEvent(QWheelEvent* event) {
  event->setAccepted(false);
  m_rootInteractionHandler.wheelEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::contextMenuEvent(QContextMenuEvent* event) {
  event->setAccepted(false);
  m_rootInteractionHandler.contextMenuEvent(event, m_interactionState);
  event->setAccepted(true);
  updateStatusTipAndCursor();
  maybeQueueRedraw();
}

void ImageViewBase::resizeEvent(QResizeEvent* event) {
  QAbstractScrollArea::resizeEvent(event);

  if (m_ignoreResizeEvents) {
    return;
  }

  const ScopedIncDec<int> guard(m_ignoreScrollEvents);

  if (maximumViewportSize() != m_lastMaximumViewportSize) {
    const TransformChangeWatcher watcher(*this);
    m_lastMaximumViewportSize = maximumViewportSize();
    if (!m_coordMapper.setWidgetFocalPoint(m_coordMapper.centeredWidgetFocalPoint(m_lastMaximumViewportSize),
                                           m_lastMaximumViewportSize)) {
      updateWidgetTransform();
    }
  } else {
    const TransformChangeWatcher watcher(*this);
    const TempFocalPointAdjuster tempFp(*this, QPointF(0, 0));
    updateTransformPreservingScale(ImagePresentation(
        m_coordMapper.imageToVirtual(), m_coordMapper.virtualImageCropArea(), m_coordMapper.virtualDisplayRect()));
  }
}

/**
 * Whether \p focused is an input field with a typed value the user hasn't confirmed yet.
 * A field merely clicked into, or confirmed with Enter, can give up the focus: otherwise one
 * would have to click into the image to get it back, and that click already starts a zone.
 */
static bool isTypingUnconfirmed(const QWidget* focused) {
  // The focus may be on the edit field inside a spin box rather than on the spin box itself.
  if (qobject_cast<const QLineEdit*>(focused) && qobject_cast<const QAbstractSpinBox*>(focused->parentWidget())) {
    focused = focused->parentWidget();
  }
  // Multi-line text has no confirmation.
  if (qobject_cast<const QTextEdit*>(focused) || qobject_cast<const QPlainTextEdit*>(focused)) {
    return true;
  }
  // A spin box shows its value formatted again once it took it (Enter, arrow keys).
  if (const auto* spinBox = qobject_cast<const QDoubleSpinBox*>(focused)) {
    return spinBox->cleanText() != spinBox->textFromValue(spinBox->value());
  }
  // Other spin boxes: typed text marks their edit field as modified; showing a new value (arrow
  // keys, or Enter when the text gets reformatted) clears that again.
  if (const auto* spinBox = qobject_cast<const QAbstractSpinBox*>(focused)) {
    const auto* edit = spinBox->findChild<const QLineEdit*>();
    return edit && edit->isModified();
  }
  if (const auto* lineEdit = qobject_cast<const QLineEdit*>(focused)) {
    return lineEdit->isModified();
  }
  return false;
}

void ImageViewBase::enterEvent(QEnterEvent* event) {
  // Taking the focus makes the keys work on the image right away, but not while the user
  // types into an input field: Delete, +/- or the arrows meant for it would act on the
  // image or delete a zone instead.
  if (!isTypingUnconfirmed(QApplication::focusWidget())) {
    viewport()->setFocus();
  }
  QAbstractScrollArea::enterEvent(event);
}

void ImageViewBase::leaveEvent(QEvent* event) {
  updateCursorPos(std::nullopt);
  QAbstractScrollArea::leaveEvent(event);
}

void ImageViewBase::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  ImageViewInfoListener* listener = m_infoListener;
  if (listener == nullptr) {
    listener = WidgetUtils::castOrFindChild<ImageViewInfoListener*>(window());
  }
  if (listener != nullptr) {
    infoProvider().addListener(listener);
  }
}

void ImageViewBase::hideEvent(QHideEvent* event) {
  infoProvider().removeAllListeners();
  QWidget::hideEvent(event);
}

/**
 * Called when any of the transformations change.
 */
void ImageViewBase::transformChanged() {
  updateScrollBars();
}

void ImageViewBase::updateScrollBars() {
  if (verticalScrollBar()->isSliderDown() || horizontalScrollBar()->isSliderDown()) {
    return;
  }

  const ScopedIncDec<int> guard1(m_ignoreScrollEvents);
  const ScopedIncDec<int> guard2(m_ignoreResizeEvents);

  const QSize maxSize = maximumViewportSize();
  QRectF currentViewport(QPointF(0, 0), maxSize);

  // Introduction of one scrollbar will decrease the available size in
  // another direction, which may cause a scrollbar in that direction
  // to become necessary. For this reason, we have a loop here.
  for (int i = 0; i < 2; ++i) {
    const ImageViewCoordMapper::ScrollBarState state = m_coordMapper.computeScrollBarState(maxSize, currentViewport);

    horizontalScrollBar()->setRange(state.xMin, state.xMax);
    verticalScrollBar()->setRange(state.yMin, state.yMax);

    horizontalScrollBar()->setValue(state.xVal);
    verticalScrollBar()->setValue(state.yVal);

    horizontalScrollBar()->setPageStep(state.xPageStep);
    verticalScrollBar()->setPageStep(state.yPageStep);

    const QRectF nextViewport(viewport()->rect());
    if (m_coordMapper.dynamicViewportRect(nextViewport) == m_coordMapper.dynamicViewportRect(currentViewport)) {
      break;
    }
    currentViewport = nextViewport;
  }
}  // ImageViewBase::updateScrollBars

void ImageViewBase::reactToScrollBars() {
  if (m_ignoreScrollEvents) {
    return;
  }

  const TransformChangeWatcher watcher(*this);
  const QPointF rawPosition(horizontalScrollBar()->value(), verticalScrollBar()->value());
  m_coordMapper.applyScrollBarPosition(rawPosition, maximumViewportSize());
}

void ImageViewBase::updateWidgetTransform() {
  const TransformChangeWatcher watcher(*this);
  m_coordMapper.updateWidgetTransform(maximumViewportSize());
}

void ImageViewBase::updateWidgetTransformAndFixFocalPoint(const FocalPointMode mode) {
  const TransformChangeWatcher watcher(*this);
  m_coordMapper.updateWidgetTransformAndFixFocalPoint(maximumViewportSize(), toMapperMode(mode));
}

HqImageCache::RenderRequest ImageViewBase::buildHqRenderRequest() const {
  const qreal dpr = viewport()->devicePixelRatioF();
  return HqImageCache::RenderRequest{
      m_coordMapper.imageToWidget(),
      dpr,
      QSize(qRound(viewport()->width() * dpr), qRound(viewport()->height() * dpr)),
  };
}

void ImageViewBase::updateStatusTipAndCursor() {
  updateStatusTip();
  updateCursor();
}

void ImageViewBase::updateStatusTip() {
  ensureStatusTip(m_interactionState.statusTip());
}

void ImageViewBase::updateCursor() {
  viewport()->setCursor(m_interactionState.cursor());
}

void ImageViewBase::maybeQueueRedraw() {
  if (m_interactionState.redrawRequested()) {
    m_interactionState.setRedrawRequested(false);
    updateViewport();
  }
}

BackgroundExecutor& ImageViewBase::backgroundExecutor() {
  static BackgroundExecutor executor;
  return executor;
}

void ImageViewBase::updateCursorPos(const std::optional<QPointF>& pos) {
  if (pos != m_cursorPos) {
    m_cursorPos = pos;
    if (!m_cursorTrackerTimer.isActive()) {
      // report cursor pos once in 150 msec
      m_cursorTrackerTimer.start(150);
    }
  }
}

void ImageViewBase::updatePhysSize() {
  m_infoProvider.setPhysSize(m_coordMapper.virtualImageCropArea().boundingRect().size());
}

ImageViewInfoProvider& ImageViewBase::infoProvider() {
  return m_infoProvider;
}

/*================= ImageViewBase::TempFocalPointAdjuster =================*/

ImageViewBase::TempFocalPointAdjuster::TempFocalPointAdjuster(ImageViewBase& obj)
    : m_obj(obj), m_origWidgetFP(obj.getWidgetFocalPoint()) {
  obj.setWidgetFocalPointWithoutMoving(obj.m_coordMapper.centeredWidgetFocalPoint(obj.maximumViewportSize()));
}

ImageViewBase::TempFocalPointAdjuster::TempFocalPointAdjuster(ImageViewBase& obj, const QPointF tempWidgetFp)
    : m_obj(obj), m_origWidgetFP(obj.getWidgetFocalPoint()) {
  obj.setWidgetFocalPointWithoutMoving(tempWidgetFp);
}

ImageViewBase::TempFocalPointAdjuster::~TempFocalPointAdjuster() {
  m_obj.setWidgetFocalPointWithoutMoving(m_origWidgetFP);
}

/*================== ImageViewBase::TransformChangeWatcher ================*/

ImageViewBase::TransformChangeWatcher::TransformChangeWatcher(ImageViewBase& owner)
    : m_owner(owner),
      m_imageToVirtual(owner.m_coordMapper.imageToVirtual()),
      m_virtualToWidget(owner.m_coordMapper.virtualToWidget()),
      m_virtualDisplayArea(owner.m_coordMapper.virtualDisplayRect()) {
  ++m_owner.m_transformChangeWatchersActive;
}

ImageViewBase::TransformChangeWatcher::~TransformChangeWatcher() {
  if (--m_owner.m_transformChangeWatchersActive == 0) {
    if ((m_imageToVirtual != m_owner.m_coordMapper.imageToVirtual())
        || (m_virtualToWidget != m_owner.m_coordMapper.virtualToWidget())
        || (m_virtualDisplayArea != m_owner.m_coordMapper.virtualDisplayRect())) {
      m_owner.transformChanged();
    }
  }
}
