// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <HoughLineDetector.h>

#include <QSize>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <stdexcept>
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

BOOST_AUTO_TEST_CASE(test_parameter_validation_and_out_of_bounds_process) {
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  BOOST_CHECK_THROW(HoughLineDetector(QSize(0, 100), 1.0, 0.0, 1.0, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, -5), 1.0, 0.0, 1.0, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, 100), 0.0, 0.0, 1.0, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, 100), qnan, 0.0, 1.0, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, 100), 1.0, inf, 1.0, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, 100), 1.0, 0.0, qnan, 1), std::invalid_argument);
  BOOST_CHECK_THROW(HoughLineDetector(QSize(100, 100), 1.0, 0.0, 1.0, 0), std::invalid_argument);

  HoughLineDetector detector(QSize(100, 100), 5.0, 0.0, 1.0, 1);
  // Out-of-bounds points must be ignored safely in Release without heap OOB writes.
  detector.process(-1000, -1000, 500);
  detector.process(10000, 10000, 500);
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
