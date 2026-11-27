// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BWColor.h>
#include <BinaryImage.h>
#include <MaxWhitespaceFinder.h>

#include <QRect>
#include <boost/test/unit_test.hpp>

namespace imageproc {
namespace tests {
BOOST_AUTO_TEST_SUITE(MaxWhitespaceFinderTestSuite)

BOOST_AUTO_TEST_CASE(test_out_of_bounds_obstacle_before_first_next) {
  MaxWhitespaceFinder finder(BinaryImage(100, 100, WHITE));
  finder.addObstacle(QRect(200, 0, 10, 10));
  BOOST_CHECK(finder.next() == QRect(0, 0, 100, 100));
}

BOOST_AUTO_TEST_CASE(test_null_and_undersized_images) {
  MaxWhitespaceFinder nullFinder{BinaryImage()};
  BOOST_CHECK(nullFinder.next().isNull());

  MaxWhitespaceFinder smallFinder(BinaryImage(10, 10, WHITE), QSize(20, 5));
  BOOST_CHECK(smallFinder.next().isNull());

  MaxWhitespaceFinder zeroMinFinder(BinaryImage(12, 8, WHITE), QSize(0, -3));
  BOOST_CHECK(zeroMinFinder.next() == QRect(0, 0, 12, 8));
}

BOOST_AUTO_TEST_CASE(test_obstacle_splits_whitespace) {
  BinaryImage img(100, 100, WHITE);
  img.fill(QRect(45, 0, 10, 100), BLACK);
  MaxWhitespaceFinder finder(img, QSize(10, 10));
  const QRect first = finder.next();
  BOOST_CHECK_EQUAL(first.width() * first.height(), 45 * 100);
  const QRect second = finder.next();
  BOOST_CHECK_EQUAL(second.width() * second.height(), 45 * 100);
  BOOST_CHECK(finder.next().isNull());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
