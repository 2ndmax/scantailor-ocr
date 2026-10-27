// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageView.h"

#include <core/OverlayStyle.h>

#include <QDebug>
#include <QPainter>
#include <algorithm>
#include <functional>
#include <utility>

#include "ImagePresentation.h"
#include "ImageTransformation.h"
#include "ProjectPages.h"

namespace page_split {
ImageView::ImageView(const QImage& image,
                     const QImage& downscaledImage,
                     const ImageTransformation& xform,
                     const PageLayout& layout,
                     std::shared_ptr<ProjectPages> pages,
                     const ImageId& imageId,
                     bool leftHalfRemoved,
                     bool rightHalfRemoved)
    : ImageViewBase(image, downscaledImage, ImagePresentation(xform.transform(), xform.resultingPreCropArea())),
      m_pages(std::move(pages)),
      m_imageId(imageId),
      m_leftUnremoveButton(std::bind(&ImageView::leftPageCenter, this)),
      m_rightUnremoveButton(std::bind(&ImageView::rightPageCenter, this)),
      m_dragHandler(*this),
      m_zoomHandler(*this),
      m_handlePixmap(OverlayStyle::handlePixmap()),
      m_virtLayout(layout),
      m_leftPageRemoved(leftHalfRemoved),
      m_rightPageRemoved(rightHalfRemoved) {
  setMouseTracking(true);

  m_leftUnremoveButton.setClickCallback(std::bind(&ImageView::unremoveLeftPage, this));
  m_rightUnremoveButton.setClickCallback(std::bind(&ImageView::unremoveRightPage, this));

  if (m_leftPageRemoved) {
    makeLastFollower(m_leftUnremoveButton);
  }
  if (m_rightPageRemoved) {
    makeLastFollower(m_rightUnremoveButton);
  }

  setupCuttersInteraction();

  rootInteractionHandler().makeLastFollower(*this);
  rootInteractionHandler().makeLastFollower(m_dragHandler);
  rootInteractionHandler().makeLastFollower(m_zoomHandler);
}

ImageView::~ImageView() = default;

void ImageView::setupCuttersInteraction() {
  const QString tip(tr("Drag the line or the handles."));
  const double hitRadius = std::max<double>(0.5 * m_handlePixmap.width(), 15.0);
  const int numCutters = m_virtLayout.numCutters();
  for (int i = 0; i < numCutters; ++i) {  // Loop over lines.

    for (int j = 0; j < 2; ++j) {  // Loop over handles.
      m_handles[i][j].setHitRadius(hitRadius);
      m_handles[i][j].setPositionCallback(std::bind(&ImageView::handlePosition, this, i, j));
      m_handles[i][j].setMoveRequestCallback(
          std::bind(&ImageView::handleMoveRequest, this, i, j, std::placeholders::_1));
      m_handles[i][j].setDragFinishedCallback(std::bind(&ImageView::dragFinished, this));

      m_handleInteractors[i][j].setObject(&m_handles[i][j]);
      m_handleInteractors[i][j].setProximityStatusTip(tip);
      makeLastFollower(m_handleInteractors[i][j]);
    }

    m_lineSegments[i].setPositionCallback(std::bind(&ImageView::linePosition, this, i));
    m_lineSegments[i].setMoveRequestCallback(std::bind(&ImageView::lineMoveRequest, this, i, std::placeholders::_1));
    m_lineSegments[i].setDragFinishedCallback(std::bind(&ImageView::dragFinished, this));

    m_lineInteractors[i].setObject(&m_lineSegments[i]);
    m_lineInteractors[i].setProximityCursor(Qt::SplitHCursor);
    m_lineInteractors[i].setInteractionCursor(Qt::SplitHCursor);
    m_lineInteractors[i].setProximityStatusTip(tip);
    makeLastFollower(m_lineInteractors[i]);
  }

  // Turn off cutters we don't need anymore.
  for (int i = numCutters; i < 2; ++i) {
    for (int j = 0; j < 2; ++j) {
      m_handleInteractors[i][j].unlink();
    }
    m_lineInteractors[i].unlink();
  }
}  // ImageView::setupCuttersInteraction

void ImageView::pageLayoutSetExternally(const PageLayout& layout) {
  m_virtLayout = layout;
  setupCuttersInteraction();
  update();
}

void ImageView::onPaint(QPainter& painter, const InteractionState& interaction) {
  painter.setRenderHint(QPainter::Antialiasing, false);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  painter.setPen(Qt::NoPen);
  const QRectF virtRect(virtualDisplayRect());

  switch (m_virtLayout.type()) {
    case PageLayout::SINGLE_PAGE_UNCUT:
      painter.setBrush(OverlayStyle::primary(50));
      painter.drawRect(virtRect);
      return;  // No Split Line will be drawn.
    case PageLayout::SINGLE_PAGE_CUT:
      painter.setBrush(OverlayStyle::primary(50));
      painter.drawPolygon(m_virtLayout.singlePageOutline());
      break;
    case PageLayout::TWO_PAGES:
      painter.setBrush(m_leftPageRemoved ? OverlayStyle::removed() : OverlayStyle::primary(50));
      painter.drawPolygon(m_virtLayout.leftPageOutline());
      painter.setBrush(m_rightPageRemoved ? OverlayStyle::removed() : OverlayStyle::secondary(50));
      painter.drawPolygon(m_virtLayout.rightPageOutline());
      break;
  }

  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setWorldTransform(QTransform());

  QPen pen(OverlayStyle::primary());
  pen.setCosmetic(true);
  pen.setWidth(2);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);

  const int numCutters = m_virtLayout.numCutters();
  for (int i = 0; i < numCutters; ++i) {
    const QLineF cutter(widgetCutterLine(i));
    painter.drawLine(cutter);

    QRectF rect(m_handlePixmap.rect());

    rect.moveCenter(cutter.p1());
    painter.drawPixmap(rect.topLeft(), m_handlePixmap);

    rect.moveCenter(cutter.p2());
    painter.drawPixmap(rect.topLeft(), m_handlePixmap);
  }
}  // ImageView::onPaint

PageLayout ImageView::widgetLayout() const {
  return m_virtLayout.transformed(virtualToWidget());
}

QLineF ImageView::widgetCutterLine(const int lineIdx) const {
  const QRectF widgetRect(virtualToWidget().mapRect(virtualDisplayRect()));
  QRectF reducedWidgetRect(reducedWidgetArea());
  reducedWidgetRect.setLeft(widgetRect.left());
  reducedWidgetRect.setRight(widgetRect.right());
  // The reason we restore original left and right boundaries is that
  // we want to allow cutter handles to go off-screen horizontally
  // but not vertically.
  QLineF line(customInscribedCutterLine(widgetLayout().cutterLine(lineIdx), reducedWidgetRect));

  if (m_handleInteractors[lineIdx][1].interactionInProgress(interactionState())) {
    line.setP1(virtualToWidget().map(virtualCutterLine(lineIdx).p1()));
  } else if (m_handleInteractors[lineIdx][0].interactionInProgress(interactionState())) {
    line.setP2(virtualToWidget().map(virtualCutterLine(lineIdx).p2()));
  }
  return line;
}

QLineF ImageView::virtualCutterLine(int lineIdx) const {
  const QRectF virtRect(virtualDisplayRect());

  QRectF widgetRect(virtualToWidget().mapRect(virtRect));
  const double delta = 0.5 * m_handlePixmap.width();
  widgetRect.adjust(0.0, delta, 0.0, -delta);

  QRectF reducedVirtRect(widgetToVirtual().mapRect(widgetRect));
  reducedVirtRect.setLeft(virtRect.left());
  reducedVirtRect.setRight(virtRect.right());
  // The reason we restore original left and right boundaries is that
  // we want to allow cutter handles to go off-screen horizontally
  // but not vertically.
  return customInscribedCutterLine(m_virtLayout.cutterLine(lineIdx), reducedVirtRect);
}

QRectF ImageView::reducedWidgetArea() const {
  const qreal delta = 0.5 * m_handlePixmap.width();
  return getOccupiedWidgetRect().adjusted(0.0, delta, 0.0, -delta);
}

/**
 * This implementation differs from PageLayout::inscribedCutterLine() in that
 * it forces the endpoints to lie on the top and bottom boundary lines.
 * Line's angle may change as a result.
 */
QLineF ImageView::customInscribedCutterLine(const QLineF& line, const QRectF& rect) {
  if (line.p1().y() == line.p2().y()) {
    // This should not happen, but if it does, we need to handle it gracefully.
    qreal middleX = 0.5 * (line.p1().x() + line.p2().x());
    middleX = qBound(rect.left(), middleX, rect.right());
    return QLineF(QPointF(middleX, rect.top()), QPointF(middleX, rect.bottom()));
  }

  QPointF topPt;
  QPointF bottomPt;

  line.intersects(QLineF(rect.topLeft(), rect.topRight()), &topPt);
  line.intersects(QLineF(rect.bottomLeft(), rect.bottomRight()), &bottomPt);

  const double topX = qBound(rect.left(), topPt.x(), rect.right());
  const double bottomX = qBound(rect.left(), bottomPt.x(), rect.right());
  return QLineF(QPointF(topX, rect.top()), QPointF(bottomX, rect.bottom()));
}

QPointF ImageView::handlePosition(int lineIdx, int handleIdx) const {
  const QLineF line(widgetCutterLine(lineIdx));
  return handleIdx == 0 ? line.p1() : line.p2();
}

ImageView::CutterLimits ImageView::cutterLimits(const int lineIdx, const QRectF& validArea) const {
  CutterLimits limits{validArea.left(), validArea.right(), validArea.left(), validArea.right()};

  const QLineF topEdge(validArea.topLeft(), validArea.topRight());
  const QLineF bottomEdge(validArea.bottomLeft(), validArea.bottomRight());
  // Where a cutter crosses the top and the bottom edge, in widget coordinates.
  const auto crossings = [&](const int idx, QPointF& top, QPointF& bottom) {
    const QLineF line(virtualToWidget().map(m_virtLayout.cutterLine(idx)));
    line.intersects(topEdge, &top);
    line.intersects(bottomEdge, &bottom);
  };
  QPointF ownTop;
  QPointF ownBottom;
  crossings(lineIdx, ownTop, ownBottom);
  const double ownX = 0.5 * (ownTop.x() + ownBottom.x());
  // The whole image, not only its visible part: a cutter scrolled out of view when zoomed in
  // must not be crossed either.
  const QRectF displayArea(virtualToWidget().mapRect(virtualDisplayRect()));

  // preventing intersection with other cutters
  for (int i = 0; i < m_virtLayout.numCutters(); i++) {
    if (i != lineIdx) {
      QPointF pTopI;
      QPointF pBottomI;
      crossings(i, pTopI, pBottomI);

      if ((pTopI.x() < displayArea.left()) || (pTopI.x() > displayArea.right()) || (pBottomI.x() < displayArea.left())
          || (pBottomI.x() > displayArea.right())) {
        continue;
      }

      // The cutters have no fixed roles (see PageLayout), so left and right follow their positions.
      const double otherX = 0.5 * (pTopI.x() + pBottomI.x());
      const bool otherIsLeft = (otherX < ownX) || ((otherX == ownX) && (i < lineIdx));
      if (otherIsLeft) {
        limits.minXTop = std::max(limits.minXTop, pTopI.x());
        limits.minXBottom = std::max(limits.minXBottom, pBottomI.x());
      } else {
        limits.maxXTop = std::min(limits.maxXTop, pTopI.x());
        limits.maxXBottom = std::min(limits.maxXBottom, pBottomI.x());
      }
    }
  }
  return limits;
}

bool ImageView::crossesOtherCutter(const int lineIdx, const QLineF& virtLine) const {
  // Over the whole height of the image, not only the visible part: when zoomed in, slanted
  // cutters may cross above or below what is shown.
  const QRectF area(virtualDisplayRect());
  const QLineF topEdge(area.topLeft(), area.topRight());
  const QLineF bottomEdge(area.bottomLeft(), area.bottomRight());
  const auto xAt = [](const QLineF& line, const QLineF& edge) {
    QPointF crossing;
    line.intersects(edge, &crossing);
    return crossing.x();
  };
  for (int i = 0; i < m_virtLayout.numCutters(); ++i) {
    if (i == lineIdx) {
      continue;
    }
    const QLineF other(m_virtLayout.cutterLine(i));
    const double distanceTop = xAt(virtLine, topEdge) - xAt(other, topEdge);
    const double distanceBottom = xAt(virtLine, bottomEdge) - xAt(other, bottomEdge);
    if (distanceTop * distanceBottom <= 0) {
      return true;
    }
  }
  return false;
}

void ImageView::setCutterLineWithoutCrossing(const int lineIdx, const QLineF& virtLine) {
  const QLineF current(virtualCutterLine(lineIdx));
  if (!crossesOtherCutter(lineIdx, virtLine) || crossesOtherCutter(lineIdx, current)) {
    // Also when the cutters cross already (e.g. from a project file): don't get stuck.
    m_virtLayout.setCutterLine(lineIdx, virtLine);
    return;
  }
  // Move as far towards the requested place as possible: the cutter stops at the other one.
  const auto between = [&](const double t) {
    return QLineF(current.p1() + t * (virtLine.p1() - current.p1()), current.p2() + t * (virtLine.p2() - current.p2()));
  };
  double allowed = 0.0;
  double blocked = 1.0;
  for (int i = 0; i < 30; ++i) {
    const double middle = 0.5 * (allowed + blocked);
    if (crossesOtherCutter(lineIdx, between(middle))) {
      blocked = middle;
    } else {
      allowed = middle;
    }
  }
  m_virtLayout.setCutterLine(lineIdx, between(allowed));
}

void ImageView::handleMoveRequest(int lineIdx, int handleIdx, const QPointF& pos) {
  const QRectF validArea(getOccupiedWidgetRect());
  const CutterLimits limits(cutterLimits(lineIdx, validArea));

  QLineF virtLine(virtualCutterLine(lineIdx));
  if (handleIdx == 0) {
    const QPointF wpt(qBound(limits.minXTop, pos.x(), limits.maxXTop), validArea.top());
    virtLine.setP1(widgetToVirtual().map(wpt));
  } else {
    const QPointF wpt(qBound(limits.minXBottom, pos.x(), limits.maxXBottom), validArea.bottom());
    virtLine.setP2(widgetToVirtual().map(wpt));
  }

  setCutterLineWithoutCrossing(lineIdx, virtLine);
  update();
}

QLineF ImageView::linePosition(int lineIdx) const {
  return widgetCutterLine(lineIdx);
}

void ImageView::lineMoveRequest(int lineIdx, QLineF line) {
  // Intersect with top and bottom.
  const QRectF validArea(getOccupiedWidgetRect());
  QPointF pTop;
  QPointF pBottom;

  line.intersects(QLineF(validArea.topLeft(), validArea.topRight()), &pTop);
  line.intersects(QLineF(validArea.bottomLeft(), validArea.bottomRight()), &pBottom);

  const CutterLimits limits(cutterLimits(lineIdx, validArea));

  // Limit movement.
  const double left = qMax(limits.minXTop - pTop.x(), limits.minXBottom - pBottom.x());
  const double right = qMax(pTop.x() - limits.maxXTop, pBottom.x() - limits.maxXBottom);
  if ((left > right) && (left > 0.0)) {
    line.translate(left, 0.0);
  } else if (right > 0.0) {
    line.translate(-right, 0.0);
  }

  setCutterLineWithoutCrossing(lineIdx, widgetToVirtual().map(line));
  update();
}

void ImageView::dragFinished() {
  // When a handle is being dragged, the other handle is displayed not
  // at the edge of the widget widget but at the edge of the image.
  // That means we have to redraw once dragging is over.
  // BTW, the only reason for displaying handles at widget's edges
  // is to make them visible and accessible for dragging.
  update();
  emit pageLayoutSetLocally(m_virtLayout);
}

QPointF ImageView::leftPageCenter() const {
  QRectF leftRect(m_virtLayout.leftPageOutline().boundingRect());
  QRectF rightRect(m_virtLayout.rightPageOutline().boundingRect());

  const double xMid = 0.5 * (leftRect.right() + rightRect.left());
  leftRect.setRight(xMid);
  rightRect.setLeft(xMid);
  return virtualToWidget().map(leftRect.center());
}

QPointF ImageView::rightPageCenter() const {
  QRectF leftRect(m_virtLayout.leftPageOutline().boundingRect());
  QRectF rightRect(m_virtLayout.rightPageOutline().boundingRect());

  const double xMid = 0.5 * (leftRect.right() + rightRect.left());
  leftRect.setRight(xMid);
  rightRect.setLeft(xMid);
  return virtualToWidget().map(rightRect.center());
}

void ImageView::unremoveLeftPage() {
  PageInfo pageInfo(m_pages->unremovePage(PageId(m_imageId, PageId::LEFT_PAGE)));
  m_leftUnremoveButton.unlink();
  m_leftPageRemoved = false;

  update();

  // We need invalidateThumbnail(PageInfo) rather than (PageId),
  // as we are updating page removal status.
  pageInfo.setId(PageId(m_imageId, PageId::SINGLE_PAGE));
  emit invalidateThumbnail(pageInfo);
}

void ImageView::unremoveRightPage() {
  PageInfo pageInfo(m_pages->unremovePage(PageId(m_imageId, PageId::RIGHT_PAGE)));
  m_rightUnremoveButton.unlink();
  m_rightPageRemoved = false;

  update();

  // We need invalidateThumbnail(PageInfo) rather than (PageId),
  // as we are updating page removal status.
  pageInfo.setId(PageId(m_imageId, PageId::SINGLE_PAGE));
  emit invalidateThumbnail(pageInfo);
}
}  // namespace page_split
