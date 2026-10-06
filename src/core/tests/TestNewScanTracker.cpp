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
  BOOST_CHECK(tracker.update({file("c.tif", 100)}, 0).empty());
  BOOST_CHECK(tracker.update({file("c.tif", 100)}, 250).empty());
  BOOST_CHECK(tracker.update({file("c.tif", 200)}, 500).empty());     // Still growing.
  BOOST_CHECK(tracker.update({file("c.tif", 200, 1)}, 750).empty());  // Touched again.
  BOOST_CHECK(tracker.update({file("c.tif", 200, 1)}, 1000).empty());
  BOOST_CHECK_EQUAL(tracker.update({file("c.tif", 200, 1)}, 1250).size(), 1u);
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
  BOOST_CHECK(!tracker.loadFailed("e.tif", 1000));
  // Tried again after being unchanged for a while.
  BOOST_CHECK(tracker.update({file("e.tif", 100)}, 1250).empty());
  BOOST_CHECK_EQUAL(tracker.update({file("e.tif", 100)}, 1500).size(), 1u);
  // Given up long after it appeared.
  BOOST_CHECK(tracker.loadFailed("e.tif", NewScanTracker::GIVE_UP_MS + 250));
  BOOST_CHECK(!tracker.hasPending());
  BOOST_CHECK(tracker.update({file("e.tif", 100)}, NewScanTracker::GIVE_UP_MS + 500).empty());
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
