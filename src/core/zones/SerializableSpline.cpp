// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SerializableSpline.h"

#include <QTransform>

#include "EditableSpline.h"
#include "XmlMarshaller.h"
#include "XmlUnmarshaller.h"

SerializableSpline::SerializableSpline(const EditableSpline& spline) {
  SplineVertex::Ptr vertex(spline.firstVertex());
  for (; vertex; vertex = vertex->next(SplineVertex::NO_LOOP)) {
    m_points.push_back(vertex->point());
  }
}

SerializableSpline::SerializableSpline(const QDomElement& el) {
  const QString pointStr("point");

  QDomNode node(el.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != pointStr) {
      continue;
    }

    m_points.push_back(XmlUnmarshaller::pointF(node.toElement()));
  }
}

// REVIEW-NOTE (2026-10): the points are stored in reverse order, while toPolygon() returns them unchanged
// -> converting a polygon and back reverses its direction. Not fixed because the reversal dates back to the
// original ScanTailor without a stated reason, and removing it would change the point order of zones in
// project files and for code that might depend on the direction.
SerializableSpline::SerializableSpline(const QPolygonF& polygon) {
  for (int i = polygon.size() - 1; i >= 0; i--) {
    m_points.push_back(polygon[i]);
  }
}

QDomElement SerializableSpline::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));

  const QString pointStr("point");
  XmlMarshaller marshaller(doc);
  for (const QPointF& pt : m_points) {
    el.appendChild(marshaller.pointF(pt, pointStr));
  }
  return el;
}

SerializableSpline SerializableSpline::transformed(const QTransform& xform) const {
  SerializableSpline transformed(*this);

  for (QPointF& pt : transformed.m_points) {
    pt = xform.map(pt);
  }
  return transformed;
}

SerializableSpline SerializableSpline::transformed(const std::function<QPointF(const QPointF&)>& xform) const {
  SerializableSpline transformed(*this);

  for (QPointF& pt : transformed.m_points) {
    pt = xform(pt);
  }
  return transformed;
}
