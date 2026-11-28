// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageViewCoordMapper.h"

#include <QLineF>
#include <QSizeF>
#include <Qt>
#include <algorithm>
#include <cmath>

#include "ImagePresentation.h"

namespace {
double adjustAxisFocalPoint(double coord, double minMargin, double maxMargin, bool centerIfFits) {
  if (centerIfFits && (minMargin + maxMargin >= 0.0)) {
    const double newMargin = 0.5 * (minMargin + maxMargin);
    return coord + (newMargin - minMargin);
  }
  if ((minMargin < 0.0) && (maxMargin > 0.0)) {
    return coord + std::min(std::fabs(minMargin), std::fabs(maxMargin));
  }
  if ((maxMargin < 0.0) && (minMargin > 0.0)) {
    return coord - std::min(std::fabs(minMargin), std::fabs(maxMargin));
  }
  return coord;
}

double clampAxisMovement(double proposed, double ideal) {
  if (ideal * proposed < 0.0) {
    return 0.0;
  }
  if (std::fabs(proposed) > std::fabs(ideal)) {
    return ideal;
  }
  return proposed;
}

struct AxisScrollParams {
  int minVal = 0;
  int maxVal = 0;
  int currentVal = 0;
  int pageStep = 0;
  double a = 1.0;
  double b = 0.0;
};

AxisScrollParams computeAxisScrollParams(double val,
                                         double vpCenter,
                                         double vpMin,
                                         double vpMax,
                                         double vpSize,
                                         double halfPicSize) {
  double minPos = 0.0;
  double maxPos = 0.0;
  if (val < vpCenter) {
    minPos = std::min<double>(val, vpMax - halfPicSize);
    maxPos = std::max<double>(vpCenter, vpMin + halfPicSize);
  } else {
    maxPos = std::max<double>(val, vpMin + halfPicSize);
    minPos = std::min<double>(vpCenter, vpMax - halfPicSize);
  }

  const auto range = static_cast<int>(std::ceil(maxPos - minPos));
  const int first = 0;
  const int last = range - 1;

  // Map scrollbar coordinates to widget coordinates of the display area center: f(x) = ax + b.
  const double a = (first == last) ? 1.0 : (maxPos - minPos) / (first - last);
  const double b = maxPos - a * first;

  const double ia = (maxPos == minPos) ? 1.0 : (first - last) / (maxPos - minPos);
  const double ib = first - ia * maxPos;

  return AxisScrollParams{
      first, last, qRound(ia * val + ib), qRound(vpSize), a, b,
  };
}
}  // namespace

ImageViewCoordMapper::ImageViewCoordMapper(const ImagePresentation& presentation, const Margins& margins)
    : m_virtualImageCropArea(presentation.cropArea()),
      m_virtualDisplayArea(presentation.displayArea()),
      m_imageToVirtual(presentation.transform()),
      m_virtualToImage(presentation.transform().inverted()),
      m_margins(margins) {}

void ImageViewCoordMapper::setPixmapScale(const QSize& imageSize, const QSize& pixmapSize) {
  m_pixmapToImage = QTransform();
  // An empty pixmap leaves the identity, rather than dividing by zero.
  if (!pixmapSize.isEmpty()) {
    m_pixmapToImage.scale(static_cast<double>(imageSize.width()) / pixmapSize.width(),
                          static_cast<double>(imageSize.height()) / pixmapSize.height());
  }
}

void ImageViewCoordMapper::initializeFocalPoints(const QSize& maxViewportSize) {
  m_widgetFocalPoint = centeredWidgetFocalPoint(maxViewportSize);
  m_pixmapFocalPoint = m_virtualToImage.map(m_virtualDisplayArea.center());
}

QRectF ImageViewCoordMapper::maxViewportRect(const QSize& maxViewportSize) const {
  return dynamicViewportRect(QRectF(QPointF(0, 0), maxViewportSize));
}

QRectF ImageViewCoordMapper::dynamicViewportRect(const QRectF& viewportRect) const {
  QRectF r(viewportRect);
  r.adjust(m_margins.left(), m_margins.top(), -m_margins.right(), -m_margins.bottom());
  if (r.isEmpty()) {
    return QRectF(viewportRect.center(), viewportRect.center());
  }
  return r;
}

QRectF ImageViewCoordMapper::occupiedWidgetRect(const QRectF& viewportRect) const {
  const QRectF widgetRect(m_virtualToWidget.mapRect(m_virtualDisplayArea));
  return widgetRect.intersected(dynamicViewportRect(viewportRect));
}

QPointF ImageViewCoordMapper::centeredWidgetFocalPoint(const QSize& maxViewportSize) const {
  return maxViewportRect(maxViewportSize).center();
}

QPointF ImageViewCoordMapper::idealWidgetFocalPoint(const QSize& maxViewportSize, const FocalPointMode mode) const {
  const QRectF displayArea(maxViewportRect(maxViewportSize));
  const QRectF imageArea(m_virtualToWidget.mapRect(m_virtualDisplayArea));

  const double leftMargin = imageArea.left() - displayArea.left();
  const double rightMargin = displayArea.right() - imageArea.right();
  const double topMargin = imageArea.top() - displayArea.top();
  const double bottomMargin = displayArea.bottom() - imageArea.bottom();

  const bool centerIfFits = (mode == FocalPointMode::CenterIfFits);
  return QPointF(adjustAxisFocalPoint(m_widgetFocalPoint.x(), leftMargin, rightMargin, centerIfFits),
                 adjustAxisFocalPoint(m_widgetFocalPoint.y(), topMargin, bottomMargin, centerIfFits));
}

void ImageViewCoordMapper::setPresentation(const ImagePresentation& presentation) {
  m_imageToVirtual = presentation.transform();
  m_virtualToImage = m_imageToVirtual.inverted();
  m_virtualImageCropArea = presentation.cropArea();
  m_virtualDisplayArea = presentation.displayArea();
}

void ImageViewCoordMapper::updateWidgetTransform(const QSize& maxViewportSize) {
  const QRectF virtRect(m_virtualDisplayArea);
  const QRectF viewportRect(maxViewportRect(maxViewportSize));
  if (virtRect.isEmpty() || viewportRect.isEmpty()) {
    // Nothing to fit (e.g. the view is collapsed by a splitter): dividing by the empty size
    // would give a singular or NaN transform, so the previous one is kept.
    return;
  }

  const QPointF virtOrigin(m_imageToVirtual.map(m_pixmapFocalPoint));
  const QPointF widgetOrigin(m_widgetFocalPoint);

  QSizeF zoom1WidgetSize(virtRect.size());
  zoom1WidgetSize.scale(viewportRect.size(), Qt::KeepAspectRatio);

  const double zoom1X = zoom1WidgetSize.width() / virtRect.width();
  const double zoom1Y = zoom1WidgetSize.height() / virtRect.height();

  QTransform xform;
  xform.translate(-virtOrigin.x(), -virtOrigin.y());
  xform *= QTransform().scale(zoom1X * m_zoom, zoom1Y * m_zoom);
  xform *= QTransform().translate(widgetOrigin.x(), widgetOrigin.y());

  m_virtualToWidget = xform;
  m_widgetToVirtual = m_virtualToWidget.inverted();
}

void ImageViewCoordMapper::updateWidgetTransformAndFixFocalPoint(const QSize& maxViewportSize,
                                                                 const FocalPointMode mode) {
  // Must precede idealWidgetFocalPoint(), which depends on m_virtualToWidget.
  updateWidgetTransform(maxViewportSize);

  const QPointF idealWidgetFp(idealWidgetFocalPoint(maxViewportSize, mode));
  if (idealWidgetFp != m_widgetFocalPoint) {
    m_widgetFocalPoint = idealWidgetFp;
    updateWidgetTransform(maxViewportSize);
  }
}

void ImageViewCoordMapper::updateTransformPreservingScale(const ImagePresentation& presentation,
                                                          const QSize& maxViewportSize) {
  // An arbitrary line in image coordinates.
  const QLineF imageLine(0.0, 0.0, 1.0, 1.0);

  const QLineF widgetLineBefore((m_imageToVirtual * m_virtualToWidget).map(imageLine));

  setPresentation(presentation);
  updateWidgetTransform(maxViewportSize);

  const QLineF widgetLineAfter((m_imageToVirtual * m_virtualToWidget).map(imageLine));

  // Both lengths are zero if the view has no area (see updateWidgetTransform()).
  if ((widgetLineBefore.length() > 0) && (widgetLineAfter.length() > 0)) {
    m_zoom *= widgetLineBefore.length() / widgetLineAfter.length();
    updateWidgetTransform(maxViewportSize);
  }
}

bool ImageViewCoordMapper::setWidgetFocalPoint(const QPointF& widgetFp, const QSize& maxViewportSize) {
  if (widgetFp != m_widgetFocalPoint) {
    m_widgetFocalPoint = widgetFp;
    updateWidgetTransform(maxViewportSize);
    return true;
  }
  return false;
}

void ImageViewCoordMapper::setWidgetFocalPointWithoutMoving(const QPointF& newWidgetFp) {
  m_widgetFocalPoint = newWidgetFp;
  m_pixmapFocalPoint = m_virtualToImage.map(m_widgetToVirtual.map(m_widgetFocalPoint));
}

bool ImageViewCoordMapper::adjustAndSetWidgetFocalPoint(const QPointF& proposedWidgetFp, const QSize& maxViewportSize) {
  const QPointF oldWidgetFp(m_widgetFocalPoint);
  setWidgetFocalPoint(proposedWidgetFp, maxViewportSize);

  const QPointF idealWidgetFp(idealWidgetFocalPoint(maxViewportSize, FocalPointMode::CenterIfFits));
  const QPointF towardsIdeal(idealWidgetFp - oldWidgetFp);
  const QPointF towardsProposed(proposedWidgetFp - oldWidgetFp);

  const QPointF movement(clampAxisMovement(towardsProposed.x(), towardsIdeal.x()),
                         clampAxisMovement(towardsProposed.y(), towardsIdeal.y()));

  const QPointF adjustedWidgetFp(oldWidgetFp + movement);
  if (adjustedWidgetFp != m_widgetFocalPoint) {
    m_widgetFocalPoint = adjustedWidgetFp;
    updateWidgetTransform(maxViewportSize);
  }
  return m_widgetFocalPoint != oldWidgetFp;
}

bool ImageViewCoordMapper::moveTowardsIdealPosition(const double pixelLength, const QSize& maxViewportSize) {
  if (pixelLength <= 0.0) {
    return false;
  }

  const QPointF idealWidgetFp(idealWidgetFocalPoint(maxViewportSize, FocalPointMode::CenterIfFits));
  if (idealWidgetFp == m_widgetFocalPoint) {
    return false;
  }

  QPointF vec(idealWidgetFp - m_widgetFocalPoint);
  const double maxLength = std::sqrt(vec.x() * vec.x() + vec.y() * vec.y());
  if (pixelLength >= maxLength) {
    m_widgetFocalPoint = idealWidgetFp;
  } else {
    vec *= pixelLength / maxLength;
    m_widgetFocalPoint += vec;
  }

  updateWidgetTransform(maxViewportSize);
  return true;
}

bool ImageViewCoordMapper::setZoomLevel(const double zoom, const QSize& maxViewportSize) {
  if (m_zoom != zoom) {
    m_zoom = zoom;
    updateWidgetTransform(maxViewportSize);
    return true;
  }
  return false;
}

ImageViewCoordMapper::ScrollBarState ImageViewCoordMapper::computeScrollBarState(const QSize& maxViewportSize,
                                                                                 const QRectF& currentViewportRect) {
  const QRectF picture(m_virtualToWidget.mapRect(m_virtualDisplayArea));
  const QPointF viewportCenter(maxViewportRect(maxViewportSize).center());
  const QPointF pictureCenter(picture.center());
  const QRectF viewport(dynamicViewportRect(currentViewportRect));

  const AxisScrollParams xParams = computeAxisScrollParams(pictureCenter.x(), viewportCenter.x(), viewport.left(),
                                                           viewport.right(), viewport.width(), 0.5 * picture.width());
  const AxisScrollParams yParams
      = computeAxisScrollParams(pictureCenter.y(), viewportCenter.y(), viewport.top(), viewport.bottom(),
                                viewport.height(), 0.5 * picture.height());

  m_scrollTransform.setMatrix(xParams.a, 0, 0, 0, yParams.a, 0, xParams.b, yParams.b, 1);

  return ScrollBarState{
      xParams.minVal, xParams.maxVal, xParams.currentVal, xParams.pageStep,
      yParams.minVal, yParams.maxVal, yParams.currentVal, yParams.pageStep,
  };
}

void ImageViewCoordMapper::applyScrollBarPosition(const QPointF& rawScrollPos, const QSize& maxViewportSize) {
  const QPointF newFp(m_scrollTransform.map(rawScrollPos));
  const QPointF oldFp(m_widgetFocalPoint);

  m_pixmapFocalPoint = m_virtualToImage.map(m_virtualDisplayArea.center());
  m_widgetFocalPoint = newFp;
  updateWidgetTransform(maxViewportSize);

  setWidgetFocalPointWithoutMoving(oldFp);
}
