// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrthogonalRotation.h"

#include <QDomDocument>
#include <QDomElement>
#include <QSize>
#include <QString>
#include <QTransform>
#include <cassert>

OrthogonalRotation::OrthogonalRotation() : m_degrees(0) {}

OrthogonalRotation::OrthogonalRotation(const QDomElement& el) : m_degrees(0) {
  // A damaged or hand-edited project file may hold any angle. transform() and rotate()
  // only handle 0, 90, 180 and 270, so full turns are removed and anything else becomes 0.
  const int degrees = ((el.attribute("degrees").toInt() % 360) + 360) % 360;
  if (degrees % 90 == 0) {
    m_degrees = degrees;
  }
}

int OrthogonalRotation::toDegrees() const {
  return m_degrees;
}

void OrthogonalRotation::nextClockwiseDirection() {
  m_degrees += 90;
  if (m_degrees == 360) {
    m_degrees = 0;
  }
}

void OrthogonalRotation::prevClockwiseDirection() {
  m_degrees -= 90;
  if (m_degrees == -90) {
    m_degrees = 270;
  }
}

QSize OrthogonalRotation::rotate(const QSize& dimensions) const {
  if ((m_degrees == 90) || (m_degrees == 270)) {
    return QSize(dimensions.height(), dimensions.width());
  } else {
    return dimensions;
  }
}

QTransform OrthogonalRotation::transform(const QSizeF& dimensions) const {
  QTransform t;

  switch (m_degrees) {
    case 0:
      return t;
    case 90:
      t.translate(dimensions.height(), 0);
      break;
    case 180:
      t.translate(dimensions.width(), dimensions.height());
      break;
    case 270:
      t.translate(0, dimensions.width());
      break;
    default:
      assert(!"Unreachable");
  }

  t.rotate(m_degrees);
  return t;
}

QDomElement OrthogonalRotation::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("degrees", m_degrees);
  return el;
}

bool OrthogonalRotation::operator==(const OrthogonalRotation& other) const {
  return m_degrees == other.m_degrees;
}

bool OrthogonalRotation::operator!=(const OrthogonalRotation& other) const {
  return !(*this == other);
}
