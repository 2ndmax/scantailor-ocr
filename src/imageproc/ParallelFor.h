// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_PARALLELFOR_H_
#define SCANTAILOR_IMAGEPROC_PARALLELFOR_H_

#include <algorithm>
#include <exception>
#include <thread>
#include <vector>

#include "ThreadBudget.h"

namespace imageproc {
/**
 * Calls body(begin, end) for consecutive slices of [0, count), one slice per thread the current task may use
 * (see ThreadBudget: all of them for a single page, one each in a full batch).
 * Each index is handled exactly once, so filters that compute every row on its own give
 * the same result as a single loop. If a body throws (e.g. std::bad_alloc), all threads
 * are still joined, and the first exception is thrown again in the calling thread.
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
  // An exception must not leave a thread function, as that calls std::terminate.
  std::vector<std::exception_ptr> errors(slices);
  const auto runSlice = [&](const int slice) {
    try {
      body(bound(slice), bound(slice + 1));
    } catch (...) {
      errors[slice] = std::current_exception();
    }
  };
  {
    std::vector<std::thread> workers;
    // Destroying a thread that is still joinable calls std::terminate, so join them even when
    // starting one of them throws.
    const auto joinAll = [&workers] {
      for (std::thread& worker : workers) {
        if (worker.joinable()) {
          worker.join();
        }
      }
    };
    try {
      workers.reserve(slices - 1);
      for (int slice = 1; slice < slices; ++slice) {
        workers.emplace_back(runSlice, slice);
      }
    } catch (...) {
      joinAll();
      throw;
    }
    runSlice(0);
    joinAll();
  }
  for (const std::exception_ptr& error : errors) {
    if (error) {
      std::rethrow_exception(error);
    }
  }
}
}  // namespace imageproc
#endif  // SCANTAILOR_IMAGEPROC_PARALLELFOR_H_
