// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutOfMemoryHandler.h"

OutOfMemoryHandler::OutOfMemoryHandler() : m_hadOOM(false) {}

OutOfMemoryHandler& OutOfMemoryHandler::instance() {
  // C++11 initializes local statics thread-safely.
  static OutOfMemoryHandler object;
  return object;
}

void OutOfMemoryHandler::allocateEmergencyMemory(size_t bytes) {
  const QMutexLocker locker(&m_mutex);

  std::unique_ptr<char[]>(new char[bytes]).swap(m_emergencyBuffer);
}

void OutOfMemoryHandler::handleOutOfMemorySituation() {
  const QMutexLocker locker(&m_mutex);

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
