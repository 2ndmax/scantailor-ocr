// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "WidthCorrection.h"

#include <QDomDocument>
#include <QStringList>
#include <algorithm>
#include <cmath>

namespace dewarping {
WidthCorrection::WidthCorrection(std::vector<double> samples) {
  if (isValid(samples)) {
    m_samples = std::move(samples);
  }
}

WidthCorrection::WidthCorrection(const QDomElement& el) {
  std::vector<double> samples;
  const QStringList values = el.attribute("samples").split(' ', Qt::SkipEmptyParts);
  for (const QString& value : values) {
    bool ok = false;
    samples.push_back(value.toDouble(&ok));
    if (!ok) {
      return;
    }
  }
  if (isValid(samples)) {
    m_samples = std::move(samples);
  }
}

QDomElement WidthCorrection::toXml(QDomDocument& doc, const QString& name) const {
  if (isNull()) {
    return QDomElement();
  }
  QStringList values;
  for (const double sample : m_samples) {
    values.push_back(QString::number(sample, 'g', 8));
  }
  QDomElement el(doc.createElement(name));
  el.setAttribute("samples", values.join(' '));
  return el;
}

double WidthCorrection::map(const double x) const {
  if (isNull() || (x <= 0.0) || (x >= 1.0)) {
    return x;
  }
  const double pos = x * static_cast<double>(m_samples.size() - 1);
  const auto idx = static_cast<size_t>(pos);
  const double frac = pos - static_cast<double>(idx);
  return m_samples[idx] + (m_samples[idx + 1] - m_samples[idx]) * frac;
}

double WidthCorrection::unmap(const double x) const {
  if (isNull() || (x <= 0.0) || (x >= 1.0)) {
    return x;
  }
  // The first sample above x; the samples rise strictly from 0 to 1.
  const auto it = std::upper_bound(m_samples.begin(), m_samples.end(), x);
  const auto idx = static_cast<size_t>(it - m_samples.begin()) - 1;
  const double frac = (x - m_samples[idx]) / (m_samples[idx + 1] - m_samples[idx]);
  return (static_cast<double>(idx) + frac) / static_cast<double>(m_samples.size() - 1);
}

bool WidthCorrection::matches(const WidthCorrection& other) const {
  if (m_samples.size() != other.m_samples.size()) {
    return false;
  }
  for (size_t i = 0; i < m_samples.size(); ++i) {
    if (std::fabs(m_samples[i] - other.m_samples[i]) > 1e-6) {
      return false;
    }
  }
  return true;
}

bool WidthCorrection::isValid(const std::vector<double>& samples) {
  if ((samples.size() < 2) || (samples.front() != 0.0) || (samples.back() != 1.0)) {
    return false;
  }
  for (size_t i = 1; i < samples.size(); ++i) {
    if (!(samples[i] > samples[i - 1])) {
      return false;
    }
  }
  return true;
}
}  // namespace dewarping
