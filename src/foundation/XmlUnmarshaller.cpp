// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "XmlUnmarshaller.h"

#include <QDomElement>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QString>
#include <algorithm>
#include <cmath>

namespace {
// Far beyond any page: the longest image side is 32767 pixels, and no length in millimeters
// comes near it either.
const double MAX_COORDINATE = 1e7;

// A damaged or hand-edited project file may hold "nan", "inf" or 1e308; geometry built from them
// becomes NaN, and Qt's polygon clipping crashed on such huge coordinates.
double coordinate(const QDomElement& el, const QString& name) {
  return XmlUnmarshaller::numberInRange(el, name, -MAX_COORDINATE, MAX_COORDINATE);
}
}  // namespace

double XmlUnmarshaller::number(const QDomElement& el, const QString& name, const double fallback) {
  bool ok = false;
  const double value = el.attribute(name).toDouble(&ok);
  return (ok && std::isfinite(value)) ? value : fallback;
}

double XmlUnmarshaller::numberInRange(const QDomElement& el,
                                      const QString& name,
                                      const double min,
                                      const double max,
                                      const double fallback) {
  return std::clamp(number(el, name, fallback), min, max);
}

QString XmlUnmarshaller::string(const QDomElement& el) {
  // QDom has already unescaped the text when reading the document.
  return el.text();
}

// size() and rect() take any int, also negative ones: what range is valid depends on the setting,
// so each reader checks its own values (with number() or its own checks).
QSize XmlUnmarshaller::size(const QDomElement& el) {
  const int width = el.attribute("width").toInt();
  const int height = el.attribute("height").toInt();
  return QSize(width, height);
}

QSizeF XmlUnmarshaller::sizeF(const QDomElement& el) {
  const double width = coordinate(el, "width");
  const double height = coordinate(el, "height");
  return QSizeF(width, height);
}

QPointF XmlUnmarshaller::pointF(const QDomElement& el) {
  const double x = coordinate(el, "x");
  const double y = coordinate(el, "y");
  return QPointF(x, y);
}

QLineF XmlUnmarshaller::lineF(const QDomElement& el) {
  const QPointF p1(pointF(el.namedItem("p1").toElement()));
  const QPointF p2(pointF(el.namedItem("p2").toElement()));
  return QLineF(p1, p2);
}

QRect XmlUnmarshaller::rect(const QDomElement& el) {
  const int x = el.attribute("x").toInt();
  const int y = el.attribute("y").toInt();
  const int width = el.attribute("width").toInt();
  const int height = el.attribute("height").toInt();
  return QRect(x, y, width, height);
}

QRectF XmlUnmarshaller::rectF(const QDomElement& el) {
  const double x = coordinate(el, "x");
  const double y = coordinate(el, "y");
  const double width = coordinate(el, "width");
  const double height = coordinate(el, "height");
  return QRectF(x, y, width, height);
}

QPolygonF XmlUnmarshaller::polygonF(const QDomElement& el) {
  QPolygonF poly;

  const QString pointTagName("point");
  QDomNode node(el.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != pointTagName) {
      continue;
    }
    poly.push_back(pointF(node.toElement()));
  }
  return poly;
}
