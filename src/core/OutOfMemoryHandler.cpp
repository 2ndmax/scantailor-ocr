// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutOfMemoryHandler.h"

#include <QCoreApplication>

OutOfMemoryHandler::OutOfMemoryHandler() : m_hadOOM(false) {
  // The first caller of instance() may be a pool thread without an event loop (tests, a command line run).
  // The signal is queued to the thread of this object, so it belongs to the main thread.
  if (QCoreApplication::instance()) {
    moveToThread(QCoreApplication::instance()->thread());
  }
}

OutOfMemoryHandler& OutOfMemoryHandler::instance() {
  // C++11 initializes local statics thread-safely.
  static OutOfMemoryHandler object;
  return object;
}

void OutOfMemoryHandler::allocateEmergencyMemory(size_t bytes) {
  const QMutexLocker locker(&m_mutex);

  // The () sets the memory to 0. Pages that are never written may not be backed by real memory (Linux),
  // and freeing them in an out-of-memory situation would then give nothing.
  std::unique_ptr<char[]>(new char[bytes]()).swap(m_emergencyBuffer);
}

void OutOfMemoryHandler::handleOutOfMemorySituation() {
  const QMutexLocker locker(&m_mutex);

  // REVIEW-NOTE (2026-10): m_hadOOM is never reset -> every later out-of-memory situation returns silently.
  // Not fixed because the main window closes after the first one and the user is told to restart.
  if (m_hadOOM) {
    return;
  }

  m_hadOOM = true;
  std::unique_ptr<char[]>().swap(m_emergencyBuffer);
  QMetaObject::invokeMethod(this, "outOfMemory", Qt::QueuedConnection);
}

bool OutOfMemoryHandler::hadOutOfMemorySituation() const {
  const QMutexLocker locker(&m_mutex);
  return m_hadOOM;
}
