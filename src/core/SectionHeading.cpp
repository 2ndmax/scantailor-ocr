// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SectionHeading.h"

namespace {
/** Space above a heading that follows other entries of the panel. */
const int SPACE_ABOVE = 8;
}  // namespace

SectionHeading::SectionHeading(QWidget* parent) : QLabel(parent) {
  initialize();
}

SectionHeading::SectionHeading(const QString& text, QWidget* parent) : QLabel(text, parent) {
  initialize();
}

void SectionHeading::initialize() {
  QFont font = this->font();
  font.setBold(true);
  setFont(font);
  setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  setFirst(false);
}

bool SectionHeading::isFirst() const {
  return m_first;
}

void SectionHeading::setFirst(const bool first) {
  m_first = first;
  setContentsMargins(0, first ? 0 : SPACE_ABOVE, 0, 0);
}
