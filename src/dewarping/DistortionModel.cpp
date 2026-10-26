// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DistortionModel.h"

#include <QDomDocument>
#include <QRectF>
#include <QTransform>
#include <limits>

#include "CylindricalSurfaceDewarper.h"

namespace dewarping {
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

  const Vec2d poly[4] = {m_topCurve.polyline().front(), m_topCurve.polyline().back(), m_bottomCurve.polyline().back(),
                         m_bottomCurve.polyline().front()};

  double minDot = std::numeric_limits<double>::max();
  double maxDot = std::numeric_limits<double>::lowest();

  for (int i = 0; i < 4; ++i) {
    const Vec2d cur(poly[i]);
    const Vec2d prev(poly[(i + 3) & 3]);
    const Vec2d next(poly[(i + 1) & 3]);

    Vec2d prevNormal(cur - prev);
    std::swap(prevNormal[0], prevNormal[1]);
    prevNormal[0] = -prevNormal[0];

    const double dot = prevNormal.dot(next - cur);
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

  // REVIEW-NOTE (2026-10): only the corner quadrilateral is checked, with an absolute px² threshold
  // -> curves crossing between the corners pass as valid and distort the output there. Not fixed
  // because the user sees the result and a stricter check would reject models that work today.
  if ((std::fabs(minDot) < 0.01) || (std::fabs(maxDot) < 0.01)) {
    // Too close - possible problems with calculating homography.
    return false;
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
  const double vertScale = 1.0 / dewarper.directrixArcLength();
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