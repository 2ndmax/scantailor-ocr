// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ParallelFor.h>
#include <ThreadBudget.h>

#include <algorithm>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <new>
#include <thread>
#include <vector>

namespace imageproc {
namespace tests {
namespace {
/** Sets the thread limit, and restores the previous one also when a check fails. */
class ThreadLimit {
 public:
  explicit ThreadLimit(const int maxThreads) : m_saved(ThreadBudget::maxThreads()) {
    ThreadBudget::setMaxThreads(maxThreads);
  }

  ~ThreadLimit() { ThreadBudget::setMaxThreads(m_saved); }

  ThreadLimit(const ThreadLimit&) = delete;
  ThreadLimit& operator=(const ThreadLimit&) = delete;

 private:
  int m_saved;
};

/**
 * Threads that each hold a task of the ThreadBudget until the object is destroyed, as pages
 * processed side by side do. They are released and joined also when a check fails.
 */
class BusyTasks {
 public:
  explicit BusyTasks(const int count) {
    for (int i = 0; i < count; ++i) {
      m_threads.emplace_back([this] {
        const ThreadBudget::TaskScope scope;
        ++m_entered;
        while (!m_done) {
          std::this_thread::yield();
        }
      });
    }
    while (m_entered < count) {
      std::this_thread::yield();
    }
  }

  ~BusyTasks() {
    m_done = true;
    for (std::thread& thread : m_threads) {
      thread.join();
    }
  }

  BusyTasks(const BusyTasks&) = delete;
  BusyTasks& operator=(const BusyTasks&) = delete;

 private:
  std::atomic<int> m_entered{0};
  std::atomic<bool> m_done{false};
  std::vector<std::thread> m_threads;
};

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
  {
    const ThreadLimit limit(1);
    BOOST_TEST(countSlices(1000) == 1);
  }
  const ThreadLimit limit(ThreadBudget::availableThreads());
  BOOST_TEST(countSlices(1000) == std::min(ThreadBudget::availableThreads(), 64));
}

BOOST_AUTO_TEST_CASE(test_runs_on_one_thread_in_a_full_batch) {
  const ThreadLimit limit(ThreadBudget::availableThreads());
  const BusyTasks others(ThreadBudget::maxThreads() - 1);
  {
    // As many pages side by side as threads: the filter of each page must not split up.
    const ThreadBudget::TaskScope scope;
    BOOST_TEST(countSlices(1000) == 1);
  }
}

// The helper threads work for the same page: what they start in turn shares the budget of the page,
// it does not get all threads again.
BOOST_AUTO_TEST_CASE(test_helper_threads_share_the_budget_of_their_task) {
  const ThreadLimit limit(ThreadBudget::availableThreads());
  const int maxThreads = ThreadBudget::maxThreads();
  if (maxThreads < 2) {
    BOOST_TEST_MESSAGE("only one thread available, skipped");
    return;
  }
  const ThreadBudget::TaskScope scope;
  std::atomic<int> mostInHelper{0};
  parallelFor(
      1000,
      [&](const int begin, const int) {
        if (begin != 0) {  // Not the calling thread.
          int threads = ThreadBudget::threadsForCurrentTask();
          int seen = mostInHelper.load();
          while (threads > seen && !mostInHelper.compare_exchange_weak(seen, threads)) {
          }
        }
      },
      1);
  BOOST_TEST(mostInHelper.load() >= 1);
  BOOST_TEST(mostInHelper.load() < maxThreads);
}

BOOST_AUTO_TEST_CASE(test_exception_in_a_helper_thread_reaches_the_caller) {
  const ThreadLimit limit(ThreadBudget::availableThreads());
  if (ThreadBudget::threadsForCurrentTask() < 2) {
    BOOST_TEST_MESSAGE("only one thread available, skipped");
    return;
  }
  // The last slice runs in a helper thread, the first one in the calling thread.
  BOOST_CHECK_THROW(parallelFor(
                        1000,
                        [](const int, const int end) {
                          if (end == 1000) {
                            throw std::bad_alloc();
                          }
                        },
                        1),
                    std::bad_alloc);
  // The calling thread throws while the helpers still run: they are joined first.
  std::atomic<int> finished{0};
  BOOST_CHECK_THROW(parallelFor(
                        1000,
                        [&](const int begin, const int) {
                          if (begin == 0) {
                            throw std::bad_alloc();
                          }
                          std::this_thread::sleep_for(std::chrono::milliseconds(20));
                          ++finished;
                        },
                        1),
                    std::bad_alloc);
  BOOST_TEST(finished == std::min(ThreadBudget::threadsForCurrentTask(), 64) - 1);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
