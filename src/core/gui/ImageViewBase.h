// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGEVIEWBASE_H_
#define SCANTAILOR_CORE_IMAGEVIEWBASE_H_

#include <QAbstractScrollArea>
#include <QImage>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QTimer>
#include <QTransform>
#include <QWidget>
#include <Qt>
#include <optional>
#include <vector>

#include "HqImageCache.h"
#include "ImagePixmapUnion.h"
#include "ImageViewCoordMapper.h"
#include "ImageViewInfoProvider.h"
#include "InteractionHandler.h"
#include "InteractionState.h"
#include "Margins.h"

class QPainter;
class BackgroundExecutor;
class ImagePresentation;
class ImageViewInfoListener;
class ZoomHandler;

/**
 * \brief The base class for widgets that display and manipulate images.
 *
 * This class operates with 3 coordinate systems:
 * \li Image coordinates, where m_image.rect() is defined.
 * \li Pixmap coordinates, where m_pixmap.rect() is defined.
 *     m_pixmap is constructed from a downscaled version of m_image.
 * \li Virtual image coordinates. We need them because we are not
 *     displaying m_image as is. Instead, we display a pre-transformed
 *     version of it. So, the virtual image coordinates reference the
 *     pixels of an imaginary image that we would get if we actually
 *     pre-transformed m_image the way we want.
 * \li Widget coordinates, where this->rect() is defined.
 *
 * \see m_pixmapToImage, m_imageToVirt, m_virtualToWidget, m_widgetToVirtual.
 */
class ImageViewBase : public QAbstractScrollArea {
  Q_OBJECT
 public:
  enum FocalPointMode { CENTER_IF_FITS, DONT_CENTER };

  /**
   * \brief ImageViewBase constructor.
   *
   * \param image The image to display.
   * \param downscaledVersion The downscaled version of \p image.
   *        If it's null, it will be created automatically.
   *        The exact scale doesn't matter.
   *        The whole idea of having a downscaled version is
   *        to speed up real-time rendering of high-resolution
   *        images. Note that the delayed high quality transform
   *        operates on the original image, not the downscaled one.
   * \param presentation Specifies transformation from image
   *        pixel coordinates to virtual image coordinates, along
   *        with some other properties.
   * \param margins Reserve extra space near the widget borders.
   *        The units are widget pixels. This reserved area may
   *        be used for custom drawing or custom controls.
   */
  ImageViewBase(const QImage& image,
                const ImagePixmapUnion& downscaledVersion,
                const ImagePresentation& presentation,
                const Margins& margins = Margins(),
                BackgroundExecutor* backgroundExecutor = nullptr);

  ~ImageViewBase() override;

  /**
   * The idea behind this accessor is being able to share a single
   * downscaled pixmap between multiple image views.
   */
  const QPixmap& downscaledPixmap() const { return m_pixmap; }

  InteractionHandler& rootInteractionHandler() { return m_rootInteractionHandler; }

  InteractionState& interactionState() { return m_interactionState; }

  const InteractionState& interactionState() const { return m_interactionState; }

  const QTransform& imageToVirtual() const { return m_coordMapper.imageToVirtual(); }

  const QTransform& virtualToImage() const { return m_coordMapper.virtualToImage(); }

  const QTransform& virtualToWidget() const { return m_coordMapper.virtualToWidget(); }

  const QTransform& widgetToVirtual() const { return m_coordMapper.widgetToVirtual(); }

  QTransform imageToWidget() const { return m_coordMapper.imageToWidget(); }

  QTransform widgetToImage() const { return m_coordMapper.widgetToImage(); }

  void updateViewport() { viewport()->update(); }

  const QRectF& virtualDisplayRect() const { return m_coordMapper.virtualDisplayRect(); }

  /**
   * Get the bounding box of the image as it appears on the screen,
   * in widget coordinates.
   */
  QRectF getOccupiedWidgetRect() const;

  /**
   * Sample the image at the given widget rectangle (e.g. for color pickup).
   * Uses the underlying image instead of widget grab, so it works with OpenGL (issue #89).
   * Returns the image region under the widget rect at any zoom, scaled down to at most
   * \p maxImageSide pixels per side, or null if out of bounds.
   */
  QImage sampleImageAtWidgetRect(const QRect& widgetRect, int maxImageSide = 31) const;

  /**
   * \brief A better version of setStatusTip().
   *
   * Unlike setStatusTip(), this method will display the tooltip
   * immediately, not when the mouse enters the widget next time.
   */
  void ensureStatusTip(const QString& statusTip);

  void registerZoomHandler(ZoomHandler* handler);

  void unregisterZoomHandler(ZoomHandler* handler);

  /**
   * \brief Get the focal point in widget coordinates.
   *
   * The typical usage pattern for this function is:
   * \code
   * QPointF fp(obj.getWidgetFocalPoint());
   * obj.setWidgetFocalPoint(fp + delta);
   * \endcode
   * As a result, the image will be moved by delta widget pixels.
   */
  QPointF getWidgetFocalPoint() const { return m_coordMapper.widgetFocalPoint(); }

  /**
   * \brief Set the focal point in widget coordinates.
   *
   * This one may be used for unrestricted dragging (with Shift button).
   *
   * \see getWidgetFocalPoint()
   */
  void setWidgetFocalPoint(const QPointF& widgetFp);

  /**
   * \brief Set the focal point in widget coordinates, after adjustring
   *        it to avoid wasting of widget space.
   *
   * This one may be used for resticted dragging (the default one in ST).
   *
   * \see getWidgetFocalPoint()
   * \see setWidgetFocalPoint()
   */
  void adjustAndSetWidgetFocalPoint(const QPointF& widgetFp);

  /**
   * \brief Sets the widget focal point and recalculates the pixmap focal
   *        focal point so that the image is not moved on screen.
   */
  void setWidgetFocalPointWithoutMoving(QPointF newWidgetFp);

  /**
   * \brief Updates image-to-virtual and recalculates
   *        virtual-to-widget transformations.
   */
  void updateTransform(const ImagePresentation& presentation);

  /**
   * \brief Same as updateTransform(), but adjusts the focal point
   *        to improve screen space usage.
   */
  void updateTransformAndFixFocalPoint(const ImagePresentation& presentation, FocalPointMode mode);

  /**
   * \brief Same as updateTransform(), but preserves the visual image scale.
   */
  void updateTransformPreservingScale(const ImagePresentation& presentation);

  /**
   * \brief Sets the zoom level.
   *
   * Zoom level 1.0 means such a zoom that makes the image fit the widget.
   * Zooming will take into account the current widget and pixmap focal
   * points. To zoom to a specific point, for example the mouse position,
   * call setWidgetFocalPointWithoutMoving() first.
   */
  void setZoomLevel(double zoom);

  /**
   * \brief Returns the current zoom level.
   * \see setZoomLevel()
   */
  double zoomLevel() const { return m_coordMapper.zoomLevel(); }

  /**
   * The image is considered ideally positioned when as little as possible
   * screen space is wasted.
   *
   * \param pixelLength The euclidean distance in widget pixels to move the image.
   *        Will be clipped if it's more than required to reach the ideal position.
   */
  void moveTowardsIdealPosition(double pixelLength);

  static BackgroundExecutor& backgroundExecutor();

  ImageViewInfoProvider& infoProvider();

  void setInfoListener(ImageViewInfoListener* listener);

 protected:
  void paintEvent(QPaintEvent* event) override;

  void keyPressEvent(QKeyEvent* event) override;

  void keyReleaseEvent(QKeyEvent* event) override;

  void mousePressEvent(QMouseEvent* event) override;

  void mouseReleaseEvent(QMouseEvent* event) override;

  void mouseDoubleClickEvent(QMouseEvent* event) override;

  void mouseMoveEvent(QMouseEvent* event) override;

  void wheelEvent(QWheelEvent* event) override;

  void contextMenuEvent(QContextMenuEvent* event) override;

  void resizeEvent(QResizeEvent* event) override;

  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;

  void showEvent(QShowEvent* event) override;

  void hideEvent(QHideEvent* event) override;

  /**
   * Returns the maximum viewport size (as if scrollbars are hidden)
   * reduced by margins.
   */
  QRectF maxViewportRect() const;

  virtual void updateCursorPos(const std::optional<QPointF>& pos);

  virtual void updatePhysSize();

 private slots:

  void updateScrollBars();

  void reactToScrollBars();

 private:
  class TempFocalPointAdjuster;

  class TransformChangeWatcher;

  void transformChanged();

  void updateWidgetTransform();

  void updateWidgetTransformAndFixFocalPoint(FocalPointMode mode);

  HqImageCache::RenderRequest buildHqRenderRequest() const;

  void updateStatusTipAndCursor();

  void updateStatusTip();

  void updateCursor();

  void maybeQueueRedraw();

  InteractionHandler m_rootInteractionHandler;

  InteractionState m_interactionState;

  /**
   * The image handle. Note that the actual data of a QPixmap lives
   * in another process on most platforms.
   */
  QPixmap m_pixmap;

  ImageViewCoordMapper m_coordMapper;

  HqImageCache m_hqCache;

  /**
   * Used to distinguish between resizes induced by scrollbars (dis)appearing
   * and other factors.
   */
  QSize m_lastMaximumViewportSize;

  ImageViewInfoProvider m_infoProvider;

  ImageViewInfoListener* m_infoListener = nullptr;

  /**
   * This timer is used for tracking the cursor position.
   */
  QTimer m_cursorTrackerTimer;

  /**
   * Current mouse pos in widget coordinates, or std::nullopt when outside the widget.
   */
  std::optional<QPointF> m_cursorPos;

  int m_transformChangeWatchersActive;

  int m_ignoreScrollEvents;

  int m_ignoreResizeEvents;

  std::vector<ZoomHandler*> m_zoomHandlers;
};


#endif  // ifndef SCANTAILOR_CORE_IMAGEVIEWBASE_H_
