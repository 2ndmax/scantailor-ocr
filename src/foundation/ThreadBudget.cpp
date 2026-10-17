// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ThreadBudget.h"

#include <QThread>
#include <algorithm>
#include <atomic>

namespace {
std::atomic<int> g_maxThreads{ThreadBudget::defaultThreads()};
std::atomic<int> g_runningTasks{0};
thread_local bool t_inTask = false;
}  // namespace

int ThreadBudget::availableThreads() {
  int threads = std::max(1, QThread::idealThreadCount());
  if (sizeof(void*) <= 4) {
    threads = std::min(threads, 2);
  }
  return threads;
}

int ThreadBudget::defaultThreads() {
  return std::max(1, availableThreads() - 2);
}

int ThreadBudget::maxThreads() {
  return g_maxThreads.load(std::memory_order_relaxed);
}

void ThreadBudget::setMaxThreads(const int threads) {
  g_maxThreads.store(std::clamp(threads, 1, availableThreads()), std::memory_order_relaxed);
}

int ThreadBudget::threadsForCurrentTask() {
  const int threads = maxThreads();
  if (!t_inTask) {
    return threads;
  }
  return std::max(1, threads / std::max(1, g_runningTasks.load(std::memory_order_relaxed)));
}

ThreadBudget::TaskScope::TaskScope() : m_outer(!t_inTask) {
  // A task started from inside a task (nested scopes) counts once.
  if (m_outer) {
    t_inTask = true;
    g_runningTasks.fetch_add(1, std::memory_order_relaxed);
  }
}

ThreadBudget::TaskScope::~TaskScope() {
  if (m_outer) {
    g_runningTasks.fetch_sub(1, std::memory_order_relaxed);
    t_inTask = false;
  }
}
