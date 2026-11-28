// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGEVIEWCOORDMAPPER_H_
#define SCANTAILOR_CORE_IMAGEVIEWCOORDMAPPER_H_

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QTransform>

#include "Margins.h"

class ImagePresentation;

/**
 * \brief Pure geometry, transform, zoom, focal-point, and scrollbar mapper for ImageViewBase.
 */
class ImageViewCoordMapper {
 public:
  enum class FocalPointMode { CenterIfFits, DontCenter };

  struct ScrollBarState {
    int xMin = 0;
    int xMax = 0;
    int xVal = 0;
    int xPageStep = 0;
    int yMin = 0;
    int yMax = 0;
    int yVal = 0;
    int yPageStep = 0;
  };

  ImageViewCoordMapper(const ImagePresentation& presentation, const Margins& margins);

  void setPixmapScale(const QSize& imageSize, const QSize& pixmapSize);

  void initializeFocalPoints(const QSize& maxViewportSize);

  QRectF maxViewportRect(const QSize& maxViewportSize) const;

  QRectF dynamicViewportRect(const QRectF& viewportRect) const;

  QRectF occupiedWidgetRect(const QRectF& viewportRect) const;

  QPointF centeredWidgetFocalPoint(const QSize& maxViewportSize) const;

  QPointF idealWidgetFocalPoint(const QSize& maxViewportSize, FocalPointMode mode) const;

  void setPresentation(const ImagePresentation& presentation);

  void updateWidgetTransform(const QSize& maxViewportSize);

  void updateWidgetTransformAndFixFocalPoint(const QSize& maxViewportSize, FocalPointMode mode);

  void updateTransformPreservingScale(const ImagePresentation& presentation, const QSize& maxViewportSize);

  bool setWidgetFocalPoint(const QPointF& widgetFp, const QSize& maxViewportSize);

  void setWidgetFocalPointWithoutMoving(const QPointF& newWidgetFp);

  bool adjustAndSetWidgetFocalPoint(const QPointF& proposedWidgetFp, const QSize& maxViewportSize);

  bool moveTowardsIdealPosition(double pixelLength, const QSize& maxViewportSize);

  bool setZoomLevel(double zoom, const QSize& maxViewportSize);

  ScrollBarState computeScrollBarState(const QSize& maxViewportSize, const QRectF& currentViewportRect);

  void applyScrollBarPosition(const QPointF& rawScrollPos, const QSize& maxViewportSize);

  const QTransform& imageToVirtual() const { return m_imageToVirtual; }

  const QTransform& virtualToImage() const { return m_virtualToImage; }

  const QTransform& virtualToWidget() const { return m_virtualToWidget; }

  const QTransform& widgetToVirtual() const { return m_widgetToVirtual; }

  QTransform imageToWidget() const { return m_imageToVirtual * m_virtualToWidget; }

  QTransform widgetToImage() const { return m_widgetToVirtual * m_virtualToImage; }

  const QTransform& pixmapToImage() const { return m_pixmapToImage; }

  const QPolygonF& virtualImageCropArea() const { return m_virtualImageCropArea; }

  const QRectF& virtualDisplayRect() const { return m_virtualDisplayArea; }

  const QPointF& widgetFocalPoint() const { return m_widgetFocalPoint; }

  const QPointF& pixmapFocalPoint() const { return m_pixmapFocalPoint; }

  double zoomLevel() const { return m_zoom; }

  const Margins& margins() const { return m_margins; }

 private:
  QTransform m_pixmapToImage;
  QPolygonF m_virtualImageCropArea;
  QRectF m_virtualDisplayArea;
  QTransform m_imageToVirtual;
  QTransform m_virtualToImage;
  QTransform m_virtualToWidget;
  QTransform m_widgetToVirtual;
  QTransform m_scrollTransform;
  QPointF m_widgetFocalPoint;
  QPointF m_pixmapFocalPoint;
  Margins m_margins;
  double m_zoom = 1.0;
};

#endif  // ifndef SCANTAILOR_CORE_IMAGEVIEWCOORDMAPPER_H_
