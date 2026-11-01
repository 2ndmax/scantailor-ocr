// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Margins.h"

#include <foundation/Utils.h>

#include <QDomDocument>
#include <QDomElement>
#include <QString>
#include <algorithm>
#include <cmath>

using namespace foundation;

namespace {
// The largest margin in mm. Beyond it, the page would get so large that its geometry overflows.
// REVIEW-NOTE (2026-11): the Margins panel allows 9999 in the selected unit, e.g. 9999 in = 253975 mm -> such a
// margin is cut to 9999 mm when the project is opened again. Not fixed because margins beyond 10 m are no real
// use, and a larger limit needs a new check against the geometry overflows this guards.
const double MAX_MARGIN_MM = 9999.0;

// A damaged or hand-edited project file may hold anything. Negative margins are valid.
double marginFromXml(const QDomElement& el, const QString& name) {
  bool ok = false;
  const double value = el.attribute(name).toDouble(&ok);
  return (ok && std::isfinite(value)) ? std::clamp(value, -MAX_MARGIN_MM, MAX_MARGIN_MM) : 0.0;
}
}  // namespace

Margins::Margins() : m_top(), m_bottom(), m_left(), m_right() {}

Margins::Margins(double left, double top, double right, double bottom)
    : m_top(top), m_bottom(bottom), m_left(left), m_right(right) {}

Margins::Margins(const QDomElement& el)
    : m_top(marginFromXml(el, "top")),
      m_bottom(marginFromXml(el, "bottom")),
      m_left(marginFromXml(el, "left")),
      m_right(marginFromXml(el, "right")) {}

QDomElement Margins::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("left", Utils::doubleToString(m_left));
  el.setAttribute("right", Utils::doubleToString(m_right));
  el.setAttribute("top", Utils::doubleToString(m_top));
  el.setAttribute("bottom", Utils::doubleToString(m_bottom));
  return el;
}
