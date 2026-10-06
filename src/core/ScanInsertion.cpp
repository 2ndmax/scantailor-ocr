// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ScanInsertion.h"

#include <QDir>
#include <QFileInfo>

namespace scan_insertion {
QString prefixedFileName(const QString& anchorFile, const QString& scanFile) {
  return QFileInfo(anchorFile).completeBaseName() + QLatin1Char('_') + QFileInfo(scanFile).fileName();
}

QString replacedDir(const QString& replacedFile) {
  return QFileInfo(replacedFile).absoluteDir().absoluteFilePath(QStringLiteral("replaced"));
}

QString replacedFilePath(const QString& replacedFile, const std::function<bool(const QString&)>& exists) {
  const QFileInfo file(replacedFile);
  const QDir dir(replacedDir(replacedFile));
  QString path = dir.absoluteFilePath(file.fileName());
  const QString suffix = file.suffix().isEmpty() ? QString() : QLatin1Char('.') + file.suffix();
  for (int number = 2; exists(path); ++number) {
    path = dir.absoluteFilePath(file.completeBaseName() + QStringLiteral(" (%1)").arg(number) + suffix);
  }
  return path;
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
