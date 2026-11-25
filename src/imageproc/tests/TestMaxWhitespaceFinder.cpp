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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
