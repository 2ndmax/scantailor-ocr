// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Zone.h"

#include <QDomDocument>

Zone::Zone(const SerializableSpline& spline, const PropertySet& props) : m_spline(spline), m_props(props) {}

Zone::Zone(const QDomElement& el, const PropertyFactory& propFactory)
    : m_spline(el.namedItem("spline").toElement()), m_props(el.namedItem("properties").toElement(), propFactory) {}

QDomElement Zone::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_spline.toXml(doc, "spline"));
  el.appendChild(m_props.toXml(doc, "properties"));
  return el;
}

namespace {
// A rectangle dragged straight up or down, or points in a row, enclose nothing. So a shape needs
// three points that do not lie on one line.
bool hasThreePointsNotInARow(const QPolygonF& shape) {
  const QPointF origin = shape.front();
  const QPointF* direction = nullptr;
  for (const QPointF& pt : shape) {
    if (pt == origin) {
      continue;
    }
    if (!direction) {
      direction = &pt;
      continue;
    }
    const QPointF a = *direction - origin;
    const QPointF b = pt - origin;
    if (a.x() * b.y() - a.y() * b.x() != 0.0) {
      return true;
    }
  }
  return false;
}
}  // namespace

bool Zone::isValid() const {
  const QPolygonF& shape = m_spline.toPolygon();

  switch (shape.size()) {
    case 0:
    case 1:
    case 2:
      return false;
    case 3:
      if (shape.front() == shape.back()) {
        return false;
      }
      // fall through
    default:
      return hasThreePointsNotInARow(shape);
  }
}
