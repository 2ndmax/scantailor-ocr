// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AutoRemovingFile.h"

#include <QFile>

AutoRemovingFile::AutoRemovingFile() = default;

AutoRemovingFile::AutoRemovingFile(const QString& filePath) : m_file(filePath) {}

AutoRemovingFile::AutoRemovingFile(AutoRemovingFile&& other) noexcept : m_file(other.release()) {}

AutoRemovingFile::~AutoRemovingFile() {
  if (!m_file.isEmpty()) {
    QFile::remove(m_file);
  }
}

AutoRemovingFile& AutoRemovingFile::operator=(AutoRemovingFile&& other) noexcept {
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

void AutoRemovingFile::reset(const QString& file) {
  // Note: oldFile must be a copy, not a reference, as m_file is about
  // to be overwritten.
  const QString oldFile(m_file);

  m_file = file;

  if (!oldFile.isEmpty() && (oldFile != m_file)) {
    QFile::remove(oldFile);
  }
}

QString AutoRemovingFile::release() {
  QString saved(m_file);
  m_file = QString();
  return saved;
}
