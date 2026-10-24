// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AtomicFileOverwriter.h"

#include <QSaveFile>

AtomicFileOverwriter::AtomicFileOverwriter() = default;

AtomicFileOverwriter::~AtomicFileOverwriter() {
  abort();
}

QIODevice* AtomicFileOverwriter::startWriting(const QString& filePath) {
  abort();

  // REVIEW-NOTE (2026-10): if the program dies between startWriting() and commit()/abort(), the
  // temporary file stays next to the target. Not fixed because it only leaves clutter in the
  // cache folder.
  m_file = std::make_unique<QSaveFile>(filePath);
  if (!m_file->open(QIODevice::WriteOnly)) {
    m_file.reset();
  }
  return m_file.get();
}

bool AtomicFileOverwriter::commit() {
  if (!m_file) {
    return false;
  }
  // QSaveFile flushes the data and writes it through to the disk before it replaces the target,
  // and reports a failure of either (e.g. a full disk); the temporary file is removed then.
  const bool committed = m_file->commit();
  m_file.reset();
  return committed;
}

void AtomicFileOverwriter::abort() {
  if (!m_file) {
    return;
  }
  // Destroyed without commit(), QSaveFile removes the temporary file.
  m_file->cancelWriting();
  m_file.reset();
}
