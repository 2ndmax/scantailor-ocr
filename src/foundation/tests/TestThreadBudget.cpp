// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ThreadBudget.h>

#include <algorithm>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <thread>
#include <vector>

namespace {
/** Restores the limit at the end of a test, so that the tests don't depend on each other. */
class MaxThreadsGuard {
 public:
  MaxThreadsGuard() : m_saved(ThreadBudget::maxThreads()) {}

  ~MaxThreadsGuard() { ThreadBudget::setMaxThreads(m_saved); }

 private:
  const int m_saved;
};

/** Runs body() while `others` further threads are inside a TaskScope as well. */
template <typename Body>
void withOtherTasks(const int others, const Body& body) {
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
  body();
  done = true;
  for (std::thread& thread : threads) {
    thread.join();
  }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FoundationThreadBudgetTestSuite)

BOOST_AUTO_TEST_CASE(test_default_leaves_two_threads) {
  const int available = ThreadBudget::availableThreads();
  BOOST_TEST(available >= 1);
  BOOST_TEST(ThreadBudget::defaultThreads() == std::max(1, available - 2));
}

BOOST_AUTO_TEST_CASE(test_limit_is_clamped) {
  const MaxThreadsGuard guard;
  ThreadBudget::setMaxThreads(0);
  BOOST_TEST(ThreadBudget::maxThreads() == 1);
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads() + 100);
  BOOST_TEST(ThreadBudget::maxThreads() == ThreadBudget::availableThreads());
}

BOOST_AUTO_TEST_CASE(test_outside_a_task_gets_the_whole_limit) {
  const MaxThreadsGuard guard;
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  BOOST_TEST(ThreadBudget::threadsForCurrentTask() == ThreadBudget::availableThreads());
}

BOOST_AUTO_TEST_CASE(test_a_single_task_gets_the_whole_limit) {
  const MaxThreadsGuard guard;
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  const ThreadBudget::TaskScope scope;
  BOOST_TEST(ThreadBudget::threadsForCurrentTask() == ThreadBudget::availableThreads());
}

BOOST_AUTO_TEST_CASE(test_tasks_side_by_side_share_the_limit) {
  const MaxThreadsGuard guard;
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  const int limit = ThreadBudget::maxThreads();
  const ThreadBudget::TaskScope scope;
  withOtherTasks(3, [&] { BOOST_TEST(ThreadBudget::threadsForCurrentTask() == std::max(1, limit / 4)); });
  // As many tasks as threads: each gets one, the total stays within the limit.
  withOtherTasks(limit - 1, [&] { BOOST_TEST(ThreadBudget::threadsForCurrentTask() == 1); });
  // When the others have finished, the remaining task gets everything again.
  BOOST_TEST(ThreadBudget::threadsForCurrentTask() == limit);
}

BOOST_AUTO_TEST_CASE(test_nested_scopes_count_once) {
  const MaxThreadsGuard guard;
  ThreadBudget::setMaxThreads(ThreadBudget::availableThreads());
  const ThreadBudget::TaskScope outer;
  {
    const ThreadBudget::TaskScope inner;
    BOOST_TEST(ThreadBudget::threadsForCurrentTask() == ThreadBudget::maxThreads());
  }
  // Leaving the inner scope must not end the outer one.
  withOtherTasks(
      1, [] { BOOST_TEST(ThreadBudget::threadsForCurrentTask() == std::max(1, ThreadBudget::maxThreads() / 2)); });
}

BOOST_AUTO_TEST_SUITE_END()
