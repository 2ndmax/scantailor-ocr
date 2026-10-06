// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ScanInsertion.h"

#include <QFileInfo>

namespace scan_insertion {
QString prefixedFileName(const QString& anchorFile, const QString& scanFile) {
  return QFileInfo(anchorFile).completeBaseName() + QLatin1Char('_') + QFileInfo(scanFile).fileName();
}
}  // namespace scan_insertion

void ScanInsertionAnchor::start(const QString& anchorFile) {
  m_anchor = anchorFile;
  m_last = anchorFile;
}

void ScanInsertionAnchor::clear() {
  m_anchor.clear();
  m_last.clear();
}

void ScanInsertionAnchor::inserted(const QString& file) {
  if (isActive()) {
    m_last = file;
  }
}

bool ScanInsertionAnchor::pageSelected(const QString& file) {
  if (!isActive() || file.isEmpty() || (file == m_anchor) || (file == m_last)) {
    return false;
  }
  start(file);
  return true;
}
