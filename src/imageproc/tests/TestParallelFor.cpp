// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ParallelFor.h>
#include <ThreadBudget.h>

#include <algorithm>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <thread>
#include <vector>

namespace imageproc {
namespace tests {
namespace {
/** The number of slices parallelFor() makes, and checks that every index is handled once. */
int countSlices(const int count) {
  std::vector<std::atomic<int>> visits(count);
  std::atomic<int> slices{0};
  parallelFor(
      count,
      [&](const int begin, const int end) {
        ++slices;
        for (int i = begin; i < end; ++i) {
          ++visits[i];
        }
      },
      1);
  for (const std::atomic<int>& v : visits) {
    BOOST_REQUIRE(v == 1);
  }
  return slices;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ParallelForTestSuite)

BOOST_AUTO_TEST_CASE(test_follows_the_thread_limit) {
  const int saved = ThreadBudget::maxThreads();
  ThreadBudget::setMaxThreads(1);
  BOOST_TEST(countSlices(1000) == 1);
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  BOOST_TEST(countSlices(1000) == std::min(ThreadBudget::availableThreads(), 64));
  ThreadBudget::setMaxThreads(saved);
}

BOOST_AUTO_TEST_CASE(test_runs_on_one_thread_in_a_full_batch) {
  const int saved = ThreadBudget::maxThreads();
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  const int others = ThreadBudget::maxThreads() - 1;
  std::atomic<int> entered{0};
  std::atomic<bool> done{false};
  std::vector<std::thread> threads;
  for (int i = 0; i < others; ++i) {
    threads.emplace_back([&] {
      const ThreadBudget::TaskScope scope;
      ++entered;
      while (!done) {
        std::this_thread::yield();
      }
    });
  }
  while (entered < others) {
    std::this_thread::yield();
  }
  {
    // As many pages side by side as threads: the filter of each page must not split up.
    const ThreadBudget::TaskScope scope;
    BOOST_TEST(countSlices(1000) == 1);
  }
  done = true;
  for (std::thread& thread : threads) {
    thread.join();
  }
  ThreadBudget::setMaxThreads(saved);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
