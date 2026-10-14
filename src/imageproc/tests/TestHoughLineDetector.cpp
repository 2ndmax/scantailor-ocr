// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <HoughLineDetector.h>

#include <QSize>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(HoughLineDetectorTestSuite)

BOOST_AUTO_TEST_CASE(test_vertical_line_distance) {
  // One angle (0 degrees), so the distance of a point is its x coordinate.
  HoughLineDetector detector(QSize(100, 100), 5.0, 0.0, 1.0, 1);
  for (int y = 0; y < 100; ++y) {
    detector.process(40, y, 1);
  }

  const std::vector<HoughLine> lines(detector.findLines(50));
  BOOST_REQUIRE_EQUAL(lines.size(), 1u);
  BOOST_CHECK_CLOSE(lines[0].distance(), 40.0, 0.01);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
