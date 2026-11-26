// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <FastQueue.h>

#include <boost/test/unit_test.hpp>
#include <stdexcept>

namespace {
struct ThrowingCopy {
  static inline int liveCount = 0;
  static inline int copiesRemainingBeforeThrow = -1;

  int value = 0;

  explicit ThrowingCopy(const int v = 0) : value(v) { ++liveCount; }

  ThrowingCopy(const ThrowingCopy& other) : value(other.value) {
    if (copiesRemainingBeforeThrow == 0) {
      throw std::runtime_error("copy failed");
    }
    if (copiesRemainingBeforeThrow > 0) {
      --copiesRemainingBeforeThrow;
    }
    ++liveCount;
  }

  ThrowingCopy& operator=(const ThrowingCopy&) = default;

  ~ThrowingCopy() { --liveCount; }
};
}  // namespace

BOOST_AUTO_TEST_SUITE(FoundationContainersTestSuite)

BOOST_AUTO_TEST_CASE(test_fast_queue_fifo_and_exception_safety) {
  FastQueue<int> queue;
  BOOST_CHECK(queue.empty());
  for (int i = 0; i < 2500; ++i) {
    queue.push(i);
  }
  FastQueue<int> copied(queue);
  for (int i = 0; i < 2500; ++i) {
    BOOST_REQUIRE(!queue.empty());
    BOOST_CHECK_EQUAL(queue.front(), i);
    queue.pop();
    BOOST_REQUIRE(!copied.empty());
    BOOST_CHECK_EQUAL(copied.front(), i);
    copied.pop();
  }
  BOOST_CHECK(queue.empty());
  BOOST_CHECK(copied.empty());

  ThrowingCopy::liveCount = 0;
  ThrowingCopy::copiesRemainingBeforeThrow = -1;
  {
    const ThrowingCopy item(42);
    FastQueue<ThrowingCopy> throwOnFirstPush;
    ThrowingCopy::copiesRemainingBeforeThrow = 0;
    BOOST_CHECK_THROW(throwOnFirstPush.push(item), std::runtime_error);
    BOOST_CHECK(throwOnFirstPush.empty());

    ThrowingCopy::copiesRemainingBeforeThrow = -1;
    FastQueue<ThrowingCopy> source;
    for (int i = 0; i < 5; ++i) {
      source.push(ThrowingCopy(i));
    }
    ThrowingCopy::copiesRemainingBeforeThrow = 2;
    BOOST_CHECK_THROW((FastQueue<ThrowingCopy>(source)), std::runtime_error);
    ThrowingCopy::copiesRemainingBeforeThrow = -1;
  }
  BOOST_CHECK_EQUAL(ThrowingCopy::liveCount, 0);
}

BOOST_AUTO_TEST_SUITE_END()
