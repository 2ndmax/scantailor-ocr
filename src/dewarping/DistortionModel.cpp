// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DistortionModel.h"

#include <QDomDocument>
#include <QRectF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "CylindricalSurfaceDewarper.h"

namespace dewarping {
namespace {
double cross2d(const QPointF& o, const QPointF& a, const QPointF& b) {
  return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

bool pointOnBoundingBox(const QPointF& p, const QPointF& a, const QPointF& b, double eps) {
  return (p.x() >= std::min(a.x(), b.x()) - eps) && (p.x() <= std::max(a.x(), b.x()) + eps)
         && (p.y() >= std::min(a.y(), b.y()) - eps) && (p.y() <= std::max(a.y(), b.y()) + eps);
}

bool segmentsIntersect(const QPointF& a1, const QPointF& a2, const QPointF& b1, const QPointF& b2) {
  const double lenA = std::hypot(a2.x() - a1.x(), a2.y() - a1.y());
  const double lenB = std::hypot(b2.x() - b1.x(), b2.y() - b1.y());
  const double maxLen = std::max(lenA, lenB);
  const double ptEps = 1e-9 * maxLen;
  if ((std::max(a1.x(), a2.x()) + ptEps < std::min(b1.x(), b2.x()))
      || (std::max(b1.x(), b2.x()) + ptEps < std::min(a1.x(), a2.x()))
      || (std::max(a1.y(), a2.y()) + ptEps < std::min(b1.y(), b2.y()))
      || (std::max(b1.y(), b2.y()) + ptEps < std::min(a1.y(), a2.y()))) {
    return false;
  }

  const double epsA = 1e-9 * lenA * maxLen + 1e-12;
  const double epsB = 1e-9 * lenB * maxLen + 1e-12;
  const double d1 = cross2d(a1, a2, b1);
  const double d2 = cross2d(a1, a2, b2);
  const double d3 = cross2d(b1, b2, a1);
  const double d4 = cross2d(b1, b2, a2);

  if (((d1 > epsA && d2 < -epsA) || (d1 < -epsA && d2 > epsA))
      && ((d3 > epsB && d4 < -epsB) || (d3 < -epsB && d4 > epsB))) {
    return true;
  }
  if (std::fabs(d1) <= epsA && pointOnBoundingBox(b1, a1, a2, ptEps)) {
    return true;
  }
  if (std::fabs(d2) <= epsA && pointOnBoundingBox(b2, a1, a2, ptEps)) {
    return true;
  }
  if (std::fabs(d3) <= epsB && pointOnBoundingBox(a1, b1, b2, ptEps)) {
    return true;
  }
  if (std::fabs(d4) <= epsB && pointOnBoundingBox(a2, b1, b2, ptEps)) {
    return true;
  }
  return false;
}

QPointF samplePolylineByFraction(const std::vector<QPointF>& poly, double t) {
  std::vector<double> cumLen(poly.size(), 0.0);
  for (size_t i = 1; i < poly.size(); ++i) {
    cumLen[i] = cumLen[i - 1] + std::hypot(poly[i].x() - poly[i - 1].x(), poly[i].y() - poly[i - 1].y());
  }
  const double totalLen = cumLen.back();
  if (!(totalLen > 0.0)) {
    return poly.front();
  }
  const double target = t * totalLen;
  for (size_t i = 1; i < poly.size(); ++i) {
    if (cumLen[i] >= target) {
      const double segLen = cumLen[i] - cumLen[i - 1];
      if (!(segLen > 0.0)) {
        return poly[i];
      }
      const double localT = (target - cumLen[i - 1]) / segLen;
      return poly[i - 1] + (poly[i] - poly[i - 1]) * localT;
    }
  }
  return poly.back();
}
}  // namespace

DistortionModel::DistortionModel() = default;

DistortionModel::DistortionModel(const QDomElement& el)
    : m_topCurve(el.namedItem("top-curve").toElement()),
      m_bottomCurve(el.namedItem("bottom-curve").toElement()),
      m_widthCorrection(el.namedItem("width-correction").toElement()) {}

QDomElement DistortionModel::toXml(QDomDocument& doc, const QString& name) const {
  if (!isValid()) {
    return QDomElement();
  }

  QDomElement el(doc.createElement(name));
  el.appendChild(m_topCurve.toXml(doc, "top-curve"));
  el.appendChild(m_bottomCurve.toXml(doc, "bottom-curve"));
  if (!m_widthCorrection.isNull()) {
    el.appendChild(m_widthCorrection.toXml(doc, "width-correction"));
  }
  return el;
}

bool DistortionModel::isValid() const {
  if (!m_topCurve.isValid() || !m_bottomCurve.isValid()) {
    return false;
  }

  const std::vector<QPointF>& topPoly = m_topCurve.polyline();
  const std::vector<QPointF>& bottomPoly = m_bottomCurve.polyline();
  const Vec2d poly[4] = {topPoly.front(), topPoly.back(), bottomPoly.back(), bottomPoly.front()};

  double minDot = std::numeric_limits<double>::max();
  double maxDot = std::numeric_limits<double>::lowest();

  for (int i = 0; i < 4; ++i) {
    const Vec2d cur(poly[i]);
    const Vec2d prev(poly[(i + 3) & 3]);
    const Vec2d next(poly[(i + 1) & 3]);

    const Vec2d prevVec(cur - prev);
    const Vec2d nextVec(next - cur);
    const double len1 = std::hypot(prevVec[0], prevVec[1]);
    const double len2 = std::hypot(nextVec[0], nextVec[1]);
    if ((len1 < 1e-6) || (len2 < 1e-6)) {
      return false;
    }

    const Vec2d prevNormal(-prevVec[1], prevVec[0]);
    const double dot = prevNormal.dot(nextVec);
    if (std::fabs(dot) < 1e-4 * len1 * len2) {
      // Degenerate or nearly collinear corner - homography would be ill-conditioned.
      return false;
    }
    if (dot < minDot) {
      minDot = dot;
    }
    if (dot > maxDot) {
      maxDot = dot;
    }
  }

  if (minDot * maxDot <= 0) {
    // Not convex.
    return false;
  }

  for (size_t i = 1; i < topPoly.size(); ++i) {
    for (size_t j = 1; j < bottomPoly.size(); ++j) {
      if (segmentsIntersect(topPoly[i - 1], topPoly[i], bottomPoly[j - 1], bottomPoly[j])) {
        return false;
      }
    }
  }

  const Vec2d topChord(topPoly.back() - topPoly.front());
  const double topLen = std::hypot(topChord[0], topChord[1]);
  if (topLen < 1e-6) {
    return false;
  }
  const double quadSign = (minDot > 0.0) ? 1.0 : -1.0;
  const Vec2d inwardUnit((quadSign / topLen) * Vec2d(-topChord[1], topChord[0]));
  const double avgHeight = 0.5
                           * (std::hypot(poly[3][0] - poly[0][0], poly[3][1] - poly[0][1])
                              + std::hypot(poly[2][0] - poly[1][0], poly[2][1] - poly[1][1]));

  for (int k = 1; k <= 9; ++k) {
    const double t = static_cast<double>(k) / 10.0;
    const QPointF topPt = samplePolylineByFraction(topPoly, t);
    const QPointF bottomPt = samplePolylineByFraction(bottomPoly, t);
    if (Vec2d(bottomPt - topPt).dot(inwardUnit) <= 1e-4 * avgHeight) {
      return false;
    }
  }

  return true;
}  // DistortionModel::isValid

bool DistortionModel::matches(const DistortionModel& other) const {
  const bool thisValid = isValid();
  const bool otherValid = other.isValid();
  if (!thisValid && !otherValid) {
    return true;
  } else if (thisValid != otherValid) {
    return false;
  }

  if (!m_topCurve.matches(other.m_topCurve)) {
    return false;
  } else if (!m_bottomCurve.matches(other.m_bottomCurve)) {
    return false;
  } else if (!m_widthCorrection.matches(other.m_widthCorrection)) {
    return false;
  }
  return true;
}

QRectF DistortionModel::modelDomain(const CylindricalSurfaceDewarper& dewarper,
                                    const QTransform& toOutput,
                                    const QRectF& outputContentRect) const {
  QRectF modelDomain(boundingBox(toOutput));

  // We not only uncurl the lines, but also stretch them in curved areas.
  // Because we don't want to reach out of the content box, we shrink
  // the model domain vertically, rather than stretching it horizontally.
  const double arcLen = dewarper.directrixArcLength();
  if (!(arcLen > 0.0) || !std::isfinite(arcLen)) {
    return QRectF();
  }
  const double vertScale = 1.0 / arcLen;
  // When scaling modelDomain, we want the following point to remain where it is.
  const QPointF scaleOrigin(outputContentRect.center());

  const double newUpperPart = (scaleOrigin.y() - modelDomain.top()) * vertScale;
  const double newHeight = modelDomain.height() * vertScale;
  modelDomain.setTop(scaleOrigin.y() - newUpperPart);
  modelDomain.setHeight(newHeight);
  return modelDomain;
}

QRectF DistortionModel::boundingBox(const QTransform& transform) const {
  double top = std::numeric_limits<double>::max();
  double left = top;
  double bottom = std::numeric_limits<double>::lowest();
  double right = bottom;

  for (QPointF pt : m_topCurve.polyline()) {
    pt = transform.map(pt);
    left = std::min<double>(left, pt.x());
    right = std::max<double>(right, pt.x());
    top = std::min<double>(top, pt.y());
    bottom = std::max<double>(bottom, pt.y());
  }

  for (QPointF pt : m_bottomCurve.polyline()) {
    pt = transform.map(pt);
    left = std::min<double>(left, pt.x());
    right = std::max<double>(right, pt.x());
    top = std::min<double>(top, pt.y());
    bottom = std::max<double>(bottom, pt.y());
  }

  if ((top > bottom) || (left > right)) {
    return QRectF();
  } else {
    return QRectF(left, top, right - left, bottom - top);
  }
}
}  // namespace dewarping
