// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <TextLineRefiner.h>

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <list>
#include <vector>

namespace Tests {
using namespace dewarping;
using namespace imageproc;

BOOST_AUTO_TEST_SUITE(TextLineRefinerTestSuite)

namespace {
std::list<std::vector<QPointF>> refine(const std::vector<QPointF>& polyline) {
  GrayImage image(QSize(200, 100));
  image.fill(255);
  const TextLineRefiner refiner(image, Dpi(200, 200), Vec2f(0.0f, 1.0f));
  std::list<std::vector<QPointF>> polylines{polyline};
  refiner.refine(polylines, /*iterations=*/10, nullptr);
  return polylines;
}

bool allFinite(const std::vector<QPointF>& polyline) {
  for (const QPointF& pt : polyline) {
    if (!std::isfinite(pt.x()) || !std::isfinite(pt.y())) {
      return false;
    }
  }
  return true;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_line_between_20_and_40_px_keeps_both_ends) {
  // 30 px used to give a single snake node (division by zero).
  const auto polylines = refine({QPointF(50, 50), QPointF(80, 50)});
  BOOST_REQUIRE_EQUAL(polylines.size(), 1u);
  BOOST_CHECK_EQUAL(polylines.front().size(), 2u);
  BOOST_CHECK(allFinite(polylines.front()));
}

BOOST_AUTO_TEST_CASE(test_repeated_first_point_gives_no_nan) {
  const auto polylines = refine({QPointF(50, 50), QPointF(50, 50), QPointF(100, 50)});
  BOOST_REQUIRE_EQUAL(polylines.size(), 1u);
  BOOST_CHECK_EQUAL(polylines.front().size(), 2u);
  BOOST_CHECK(allFinite(polylines.front()));
}

// The old code looped forever on these, so they are kept apart from the tests above.
BOOST_AUTO_TEST_CASE(test_line_shorter_than_20_px_terminates) {
  const auto polylines = refine({QPointF(50, 50), QPointF(60, 50)});
  BOOST_REQUIRE_EQUAL(polylines.size(), 1u);
  BOOST_CHECK_EQUAL(polylines.front().size(), 2u);
  BOOST_CHECK(allFinite(polylines.front()));
}

// The snake has to reach the end of the line. In float arithmetic the position of the last node
// could come out a little beyond the length, and the last node was left out.
BOOST_AUTO_TEST_CASE(test_snake_keeps_the_end_of_the_line) {
  GrayImage image(QSize(2200, 100));
  image.fill(255);
  const TextLineRefiner refiner(image, Dpi(200, 200), Vec2f(0.0f, 1.0f));
  int shortened = 0;
  for (double length = 40.0; length < 2040.0; length += 0.73) {
    const QPointF end(50 + length, 50);
    std::list<std::vector<QPointF>> polylines{{QPointF(50, 50), QPointF(50 + length / 3, 50), end}};
    refiner.refine(polylines, /*iterations=*/0, nullptr);
    const std::vector<QPointF>& snake = polylines.front();
    if (snake.empty() || (std::abs(snake.back().x() - end.x()) > 0.5)) {
      ++shortened;
    }
  }
  BOOST_CHECK_EQUAL(shortened, 0);
}

BOOST_AUTO_TEST_CASE(test_degenerate_lines_terminate) {
  BOOST_CHECK(refine({QPointF(50, 50)}).front().empty());
  BOOST_CHECK(refine({QPointF(50, 50), QPointF(50, 50)}).front().empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
