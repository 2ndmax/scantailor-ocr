// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputSizeOptions.h"

#include <QDomDocument>
#include <QDomElement>
#include <algorithm>
#include <cmath>

#include "Alignment.h"
#include "Margins.h"

namespace page_layout {
namespace {
// The range of the target size in the Margins step. NaN from a damaged project file would pass
// the check for an empty size and become the scale factor of the page.
const double MAX_TARGET_SIZE_MM = 99999.0;

double targetSizeFromXml(const QDomElement& el, const QString& name) {
  const double value = el.attribute(name).toDouble();
  return std::isfinite(value) ? std::clamp(value, 0.0, MAX_TARGET_SIZE_MM) : 0.0;
}
}  // namespace

OutputSizeOptions::OutputSizeOptions(const QDomElement& el) {
  if (el.isNull()) {
    return;
  }
  m_ownTargetSize = el.attribute("ownTarget") == "1";
  m_targetSizeMm = QSizeF(targetSizeFromXml(el, "width"), targetSizeFromXml(el, "height"));
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

double OutputSizeOptions::scaleFactor(const QSizeF& contentSizeMm,
                                      const Margins& hardMarginsMm,
                                      const Alignment& alignment,
                                      const QSizeF& commonSizeMm) const {
  // Filling with margins up to the common size is how it always was: never scaled.
  if (alignment.isNull() || (!alignment.isScaleToFit() && !m_ownTargetSize)) {
    return 1.0;
  }
  const QSizeF targetSizeMm(targetSizeMM(commonSizeMm));
  if (contentSizeMm.isEmpty() || targetSizeMm.isEmpty()) {
    return 1.0;
  }

  const double availableWidth = targetSizeMm.width() - hardMarginsMm.left() - hardMarginsMm.right();
  const double availableHeight = targetSizeMm.height() - hardMarginsMm.top() - hardMarginsMm.bottom();
  double fit = std::min(availableWidth / contentSizeMm.width(), availableHeight / contentSizeMm.height());
  // The largest page fits the common size exactly; don't scale it by a rounding error.
  if (std::abs(fit - 1.0) < 1e-6) {
    fit = 1.0;
  }

  const double minFactor = m_minReducePercent / 100.0;
  const double maxFactor = alignment.isScaleToFit() ? m_maxEnlargePercent / 100.0 : 1.0;
  return std::clamp(fit, minFactor, maxFactor);
}
}  // namespace page_layout
