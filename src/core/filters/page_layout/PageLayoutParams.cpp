// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageLayoutParams.h"

#include <Margins.h>

#include <QDomDocument>
#include <QDomElement>

#include "Alignment.h"
#include "OutputSizeOptions.h"

namespace page_layout {
PageLayoutParams::PageLayoutParams(const Margins& hardMargins, const Alignment& alignment, bool autoMargins)
    : m_hardMargins(hardMargins), m_alignment(alignment), m_autoMargins(autoMargins) {}

PageLayoutParams::PageLayoutParams() : m_hardMargins(10, 5, 10, 5), m_autoMargins(false) {}

PageLayoutParams::PageLayoutParams(const QDomElement& el)
    : m_hardMargins(el.namedItem("hardMargins").toElement()),
      m_alignment(el.namedItem("alignment").toElement()),
      m_autoMargins(el.attribute("autoMargins") == "1"),
      m_outputSizeOptions(el.namedItem("output-size").toElement()) {}

QDomElement PageLayoutParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_hardMargins.toXml(doc, "hardMargins"));
  el.appendChild(m_alignment.toXml(doc, "alignment"));
  el.setAttribute("autoMargins", m_autoMargins ? "1" : "0");
  if (!m_outputSizeOptions.isDefault()) {
    el.appendChild(m_outputSizeOptions.toXml(doc, "output-size"));
  }
  return el;
}

const Margins& PageLayoutParams::getHardMargins() const {
  return m_hardMargins;
}

void PageLayoutParams::setHardMargins(const Margins& hardMargins) {
  m_hardMargins = hardMargins;
}

const Alignment& PageLayoutParams::getAlignment() const {
  return m_alignment;
}

void PageLayoutParams::setAlignment(const Alignment& alignment) {
  m_alignment = alignment;
}

bool PageLayoutParams::isAutoMargins() const {
  return m_autoMargins;
}

void PageLayoutParams::setAutoMargins(bool autoMargins) {
  m_autoMargins = autoMargins;
}

const OutputSizeOptions& PageLayoutParams::getOutputSizeOptions() const {
  return m_outputSizeOptions;
}

void PageLayoutParams::setOutputSizeOptions(const OutputSizeOptions& options) {
  m_outputSizeOptions = options;
}
}  // namespace page_layout