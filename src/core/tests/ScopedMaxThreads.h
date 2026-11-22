// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TESTS_SCOPEDMAXTHREADS_H_
#define SCANTAILOR_CORE_TESTS_SCOPEDMAXTHREADS_H_

#include <ThreadBudget.h>

namespace Tests {
/**
 * Sets ThreadBudget::maxThreads() for one test and restores it afterwards, also when the test
 * fails, so the tests after it run with the usual number.
 */
class ScopedMaxThreads {
 public:
  explicit ScopedMaxThreads(const int threads) : m_previous(ThreadBudget::maxThreads()) {
    ThreadBudget::setMaxThreads(threads);
  }

  ~ScopedMaxThreads() { ThreadBudget::setMaxThreads(m_previous); }

  ScopedMaxThreads(const ScopedMaxThreads&) = delete;
  ScopedMaxThreads& operator=(const ScopedMaxThreads&) = delete;

 private:
  const int m_previous;
};
}  // namespace Tests

#endif  // SCANTAILOR_CORE_TESTS_SCOPEDMAXTHREADS_H_
