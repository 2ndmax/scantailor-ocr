// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DetectionOptions.h"

#include <QDomElement>
#include <QtGlobal>

namespace select_content {
DetectionOptions::DetectionOptions(const QDomElement& el) {
  bool ok = false;
  const int fillFactor = el.attribute("fillFactor").toInt(&ok);
  if (ok) {
    m_fillFactor = qBound(MIN_FILL_FACTOR, fillFactor, MAX_FILL_FACTOR);
  }
  const int borderTolerance = el.attribute("borderTolerance").toInt(&ok);
  if (ok) {
    m_borderTolerance = qBound(0, borderTolerance, MAX_BORDER_TOLERANCE);
  }
  m_ignorePageBox = (el.attribute("ignorePageBox") == "1");
}

void DetectionOptions::toXml(QDomElement& el) const {
  el.setAttribute("fillFactor", m_fillFactor);
  el.setAttribute("borderTolerance", m_borderTolerance);
  el.setAttribute("ignorePageBox", m_ignorePageBox ? "1" : "0");
}
}  // namespace select_content
