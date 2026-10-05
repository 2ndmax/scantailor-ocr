// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PdfPageOrder.h>

#include <QStringList>
#include <boost/test/unit_test.hpp>

namespace {
using Items = std::vector<PdfPageOrder::Item>;

/** "a b- c" means: a ticked, b not ticked, c ticked. */
Items items(const QString& text) {
  Items result;
  for (QString key : text.split(' ', Qt::SkipEmptyParts)) {
    bool checked = true;
    if (key.endsWith('-')) {
      key.chop(1);
      checked = false;
    }
    result.push_back(PdfPageOrder::Item{key, checked});
  }
  return result;
}

std::vector<QString> keys(const QString& text) {
  std::vector<QString> result;
  for (const QString& key : text.split(' ', Qt::SkipEmptyParts)) {
    result.push_back(key);
  }
  return result;
}

QString toText(const Items& order) {
  QStringList parts;
  for (const PdfPageOrder::Item& item : order) {
    parts.push_back(item.checked ? item.key : item.key + '-');
  }
  return parts.join(' ');
}

QString merged(const QString& previous, const QString& projectOrder) {
  return toText(PdfPageOrder::merge(items(previous), keys(projectOrder)));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PdfPageOrderTestSuite)

BOOST_AUTO_TEST_CASE(test_first_time_is_project_order) {
  BOOST_CHECK(merged("", "a b c") == "a b c");
  BOOST_CHECK(merged("", "") == "");
}

BOOST_AUTO_TEST_CASE(test_unchanged_project_keeps_order_and_ticks) {
  BOOST_CHECK(merged("c a- b", "a b c") == "c a- b");
}

BOOST_AUTO_TEST_CASE(test_removed_pages_are_dropped) {
  BOOST_CHECK(merged("c a- b", "a c") == "c a-");
  BOOST_CHECK(merged("a b", "") == "");
}

BOOST_AUTO_TEST_CASE(test_new_pages_follow_their_predecessor) {
  // Without reordering, the result is the project order.
  BOOST_CHECK(merged("a b- c", "a x b y z c w") == "a x b- y z c w");
  // After reordering, a new page follows the page before it in the project.
  BOOST_CHECK(merged("c a- b", "a b x c") == "c a- b x");
  BOOST_CHECK(merged("c a- b", "a x b c") == "c a- x b");
}

BOOST_AUTO_TEST_CASE(test_new_first_page_goes_to_the_start) {
  BOOST_CHECK(merged("b c", "x b c") == "x b c");
  BOOST_CHECK(merged("c b", "x a b c") == "x a c b");
  BOOST_CHECK(merged("c b", "x y b c") == "x y c b");
}

BOOST_AUTO_TEST_CASE(test_renamed_page) {
  // A page split into a left and a right page: two new keys in its place.
  BOOST_CHECK(merged("a b- c", "a b_1L b_1R c") == "a b_1L b_1R c");
}

BOOST_AUTO_TEST_CASE(test_duplicates_in_previous_are_ignored) {
  BOOST_CHECK(merged("a b a-", "a b") == "a b");
}

BOOST_AUTO_TEST_SUITE_END()
