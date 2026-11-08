// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_INTERACTION_RECTHANDLEGEOMETRY_H_
#define SCANTAILOR_INTERACTION_RECTHANDLEGEOMETRY_H_

#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <algorithm>

/** The edges of a rectangle with handles. A corner is the mask of its two edges. */
enum RectEdge { RECT_LEFT = 1, RECT_RIGHT = 2, RECT_TOP = 4, RECT_BOTTOM = 8 };

/** The corner of \p rect given by \p edgeMask. */
inline QPointF rectCorner(const QRectF& rect, const int edgeMask) {
  QPointF point;

  if (edgeMask & RECT_TOP) {
    point.setY(rect.top());
  } else if (edgeMask & RECT_BOTTOM) {
    point.setY(rect.bottom());
  }

  if (edgeMask & RECT_LEFT) {
    point.setX(rect.left());
  } else if (edgeMask & RECT_RIGHT) {
    point.setX(rect.right());
  }
  return point;
}

/** The edge of \p rect as a line. Anything but top, bottom or left means the right edge. */
inline QLineF rectEdge(const QRectF& rect, const int edge) {
  if (edge == RECT_TOP) {
    return QLineF(rect.topLeft(), rect.topRight());
  } else if (edge == RECT_BOTTOM) {
    return QLineF(rect.bottomLeft(), rect.bottomRight());
  } else if (edge == RECT_LEFT) {
    return QLineF(rect.topLeft(), rect.bottomLeft());
  } else {
    return QLineF(rect.topRight(), rect.bottomRight());
  }
}

/** \p rect with the edges in \p edgeMask moved to \p pos, keeping at least \p minSize. */
inline QRectF resizeRectByEdges(QRectF rect, const int edgeMask, const QPointF& pos, const QSizeF& minSize) {
  if (edgeMask & RECT_TOP) {
    rect.setTop(std::min(pos.y(), rect.bottom() - minSize.height()));
  } else if (edgeMask & RECT_BOTTOM) {
    rect.setBottom(std::max(pos.y(), rect.top() + minSize.height()));
  }

  if (edgeMask & RECT_LEFT) {
    rect.setLeft(std::min(pos.x(), rect.right() - minSize.width()));
  } else if (edgeMask & RECT_RIGHT) {
    rect.setRight(std::max(pos.x(), rect.left() + minSize.width()));
  }
  return rect;
}

#endif  // SCANTAILOR_INTERACTION_RECTHANDLEGEOMETRY_H_
