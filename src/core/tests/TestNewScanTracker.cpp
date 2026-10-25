// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <NewScanTracker.h>

#include <boost/test/unit_test.hpp>

namespace {
const QDateTime kTime(QDate(2026, 10, 6), QTime(10, 0, 0));

NewScanTracker::FileState file(const QString& path, qint64 size, int secs = 0) {
  NewScanTracker::FileState state;
  state.path = path;
  state.size = size;
  state.modified = kTime.addSecs(secs);
  return state;
}

/** Updates with the same listing until something is ready, returns the number of updates. */
int updatesUntilReady(NewScanTracker& tracker, const std::vector<NewScanTracker::FileState>& listing) {
  for (int i = 1; i <= 10; ++i) {
    if (!tracker.update(listing, i * 250).empty()) {
      return i;
    }
  }
  return -1;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(NewScanTrackerTestSuite)

BOOST_AUTO_TEST_CASE(test_existing_files_are_ignored) {
  NewScanTracker tracker;
  tracker.reset({"a.tif"});
  BOOST_CHECK_EQUAL(updatesUntilReady(tracker, {file("a.tif", 100)}), -1);
  BOOST_CHECK(!tracker.hasPending());
}

BOOST_AUTO_TEST_CASE(test_new_file_ready_once_stable) {
  NewScanTracker tracker;
  tracker.reset({});
  // Seen first, then unchanged over STABLE_CHECKS checks.
  BOOST_CHECK_EQUAL(updatesUntilReady(tracker, {file("b.tif", 100)}), NewScanTracker::STABLE_CHECKS + 1);
  // Not returned again while it is being loaded.
  BOOST_CHECK(tracker.update({file("b.tif", 100)}, 5000).empty());
  tracker.done("b.tif");
  BOOST_CHECK(!tracker.hasPending());
  BOOST_CHECK(tracker.update({file("b.tif", 100)}, 6000).empty());
}

BOOST_AUTO_TEST_CASE(test_growing_file_waits) {
  NewScanTracker tracker;
  tracker.reset({});
  qint64 now = 0;
  BOOST_CHECK(tracker.update({file("c.tif", 100)}, now).empty());
  // Still growing, then touched again: the count of unchanged checks starts over each time.
  for (const auto& changed : {file("c.tif", 200), file("c.tif", 200, 1)}) {
    now += 250;
    BOOST_CHECK(tracker.update({changed}, now).empty());
  }
  // Unchanged now, so it takes STABLE_CHECKS more checks, and not one fewer.
  for (int i = 0; i < NewScanTracker::STABLE_CHECKS; ++i) {
    now += 250;
    BOOST_CHECK(tracker.update({file("c.tif", 200, 1)}, now).empty() == (i < NewScanTracker::STABLE_CHECKS - 1));
  }
}

BOOST_AUTO_TEST_CASE(test_empty_file_waits) {
  NewScanTracker tracker;
  tracker.reset({});
  BOOST_CHECK_EQUAL(updatesUntilReady(tracker, {file("d.tif", 0)}), -1);
  BOOST_CHECK(tracker.hasPending());
}

BOOST_AUTO_TEST_CASE(test_failed_load_is_retried_then_given_up) {
  NewScanTracker tracker;
  tracker.reset({});
  BOOST_REQUIRE_EQUAL(updatesUntilReady(tracker, {file("e.tif", 100)}), NewScanTracker::STABLE_CHECKS + 1);
  qint64 now = 1000;
  BOOST_CHECK(!tracker.loadFailed("e.tif", now));
  // Tried again after STABLE_CHECKS unchanged checks.
  for (int i = 0; i < NewScanTracker::STABLE_CHECKS - 1; ++i) {
    now += 250;
    BOOST_CHECK(tracker.update({file("e.tif", 100)}, now).empty());
  }
  now += 250;
  BOOST_CHECK_EQUAL(tracker.update({file("e.tif", 100)}, now).size(), 1u);
  // Given up long after it appeared.
  BOOST_CHECK(tracker.loadFailed("e.tif", NewScanTracker::GIVE_UP_MS + 250));
  BOOST_CHECK(!tracker.hasPending());
  BOOST_CHECK(tracker.update({file("e.tif", 100)}, NewScanTracker::GIVE_UP_MS + 500).empty());
}

// A placeholder the scanning program leaves behind (or a log that keeps growing) never becomes stable.
// It must not keep the folder watcher in its fast polling for ever.
BOOST_AUTO_TEST_CASE(test_never_stable_file_is_given_up) {
  NewScanTracker tracker;
  tracker.reset({});
  BOOST_CHECK(tracker.update({file("empty.tif", 0)}, 0).empty());
  BOOST_CHECK(tracker.hasPending());
  BOOST_CHECK(tracker.update({file("empty.tif", 0)}, NewScanTracker::GIVE_UP_MS - 250).empty());
  BOOST_CHECK(tracker.hasPending());
  BOOST_CHECK(tracker.update({file("empty.tif", 0)}, NewScanTracker::GIVE_UP_MS).empty());
  BOOST_CHECK(!tracker.hasPending());
  // And it stays ignored.
  BOOST_CHECK(tracker.update({file("empty.tif", 0)}, NewScanTracker::GIVE_UP_MS + 250).empty());
  BOOST_CHECK(!tracker.hasPending());

  // Same for a file that changes at every check.
  tracker.reset({});
  qint64 now = 0;
  for (int size = 1; now < NewScanTracker::GIVE_UP_MS; ++size, now += 250) {
    BOOST_CHECK(tracker.update({file("log.txt", size)}, now).empty());
  }
  BOOST_CHECK(tracker.update({file("log.txt", 9999)}, now).empty());
  BOOST_CHECK(!tracker.hasPending());
}

BOOST_AUTO_TEST_CASE(test_vanished_file_is_forgotten) {
  NewScanTracker tracker;
  tracker.reset({});
  tracker.update({file("tmp.tif", 100)}, 0);
  BOOST_CHECK(tracker.hasPending());
  tracker.update({}, 250);
  BOOST_CHECK(!tracker.hasPending());
}

BOOST_AUTO_TEST_SUITE_END()
