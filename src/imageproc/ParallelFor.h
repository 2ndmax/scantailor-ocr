// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_PARALLELFOR_H_
#define SCANTAILOR_IMAGEPROC_PARALLELFOR_H_

#include <algorithm>
#include <thread>
#include <vector>

#include "ThreadBudget.h"

namespace imageproc {
/**
 * Calls body(begin, end) for consecutive slices of [0, count), one slice per thread the current task may use
 * (see ThreadBudget: all of them for a single page, one each in a full batch).
 * Each index is handled exactly once, so filters that compute every row on its own give
 * the same result as a single loop.  The body must not throw.
 *
 * @param minPerSlice Slices are not made smaller than this, to keep the thread overhead low.
 */
template <typename Body>
void parallelFor(const int count, const Body& body, const int minPerSlice = 32) {
  const int threads = ThreadBudget::threadsForCurrentTask();
  const int slices = std::clamp(std::min(threads, count / std::max(minPerSlice, 1)), 1, 64);
  if (slices <= 1) {
    body(0, count);
    return;
  }
  const auto bound = [&](const int slice) { return static_cast<int>(static_cast<long long>(count) * slice / slices); };
  std::vector<std::thread> workers;
  workers.reserve(slices - 1);
  for (int slice = 1; slice < slices; ++slice) {
    workers.emplace_back([&, slice] { body(bound(slice), bound(slice + 1)); });
  }
  body(0, bound(1));
  for (std::thread& worker : workers) {
    worker.join();
  }
}
}  // namespace imageproc
#endif  // SCANTAILOR_IMAGEPROC_PARALLELFOR_H_
