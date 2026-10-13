// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputSizeOptions.h"

#include <QDomDocument>
#include <QDomElement>
#include <algorithm>

namespace page_layout {
OutputSizeOptions::OutputSizeOptions(const QDomElement& el) {
  if (el.isNull()) {
    return;
  }
  m_ownTargetSize = el.attribute("ownTarget") == "1";
  m_targetSizeMm = QSizeF(el.attribute("width").toDouble(), el.attribute("height").toDouble());
  setMaxEnlargePercent(el.attribute("maxEnlarge", QString::number(DEFAULT_MAX_ENLARGE_PERCENT)).toInt());
  setMinReducePercent(el.attribute("minReduce", QString::number(DEFAULT_MIN_REDUCE_PERCENT)).toInt());
}

QDomElement OutputSizeOptions::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("ownTarget", m_ownTargetSize ? "1" : "0");
  el.setAttribute("width", m_targetSizeMm.width());
  el.setAttribute("height", m_targetSizeMm.height());
  el.setAttribute("maxEnlarge", m_maxEnlargePercent);
  el.setAttribute("minReduce", m_minReducePercent);
  return el;
}

bool OutputSizeOptions::operator==(const OutputSizeOptions& other) const {
  return m_ownTargetSize == other.m_ownTargetSize && m_targetSizeMm == other.m_targetSizeMm
         && m_maxEnlargePercent == other.m_maxEnlargePercent && m_minReducePercent == other.m_minReducePercent;
}

bool OutputSizeOptions::operator!=(const OutputSizeOptions& other) const {
  return !(*this == other);
}

bool OutputSizeOptions::isDefault() const {
  return *this == OutputSizeOptions();
}

void OutputSizeOptions::setMaxEnlargePercent(const int percent) {
  m_maxEnlargePercent = std::clamp(percent, 100, 1000);
}

void OutputSizeOptions::setMinReducePercent(const int percent) {
  m_minReducePercent = std::clamp(percent, 1, 100);
}

QSizeF OutputSizeOptions::targetSizeMM(const QSizeF& commonSizeMm) const {
  if (m_ownTargetSize && !m_targetSizeMm.isEmpty()) {
    return m_targetSizeMm;
  }
  return commonSizeMm;
}
}  // namespace page_layout
