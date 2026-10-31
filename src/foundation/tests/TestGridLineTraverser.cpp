// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GridLineTraverser.h>

#include <QLineF>
#include <QPoint>
#include <QRect>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
std::vector<QPoint> traverse(const QLineF& line) {
  std::vector<QPoint> points;
  GridLineTraverser traverser(line);
  while (traverser.hasNext()) {
    points.push_back(traverser.next());
  }
  return points;
}

// The callers clip the line to their grid and rely on this to index it.
void checkStaysBetweenEndpoints(const QLineF& line) {
  const QPoint p1 = line.p1().toPoint();
  const QPoint p2 = line.p2().toPoint();
  const QRect box(QPoint(std::min(p1.x(), p2.x()), std::min(p1.y(), p2.y())),
                  QPoint(std::max(p1.x(), p2.x()), std::max(p1.y(), p2.y())));
  const std::vector<QPoint> points = traverse(line);
  BOOST_REQUIRE(!points.empty());
  for (const QPoint& pt : points) {
    BOOST_CHECK_MESSAGE(box.contains(pt), "point (" << pt.x() << ", " << pt.y() << ") lies outside the box ("
                                                    << box.left() << ", " << box.top() << ") - (" << box.right() << ", "
                                                    << box.bottom() << ")");
  }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FoundationGridLineTraverserTestSuite)

// A line that ends on the last row of a 96 x 72 grid: extended to the next whole column, its
// end lay at y = 71.5, which rounded to the row below the grid. TopBottomEdgeTracer then read and
// wrote outside its grid (found by fuzz_page_output).
BOOST_AUTO_TEST_CASE(points_stay_between_the_rounded_endpoints) {
  const QLineF line(QPointF(0, 25.5), QPointF(45.5, 71));
  checkStaysBetweenEndpoints(line);
  checkStaysBetweenEndpoints(QLineF(line.p2(), line.p1()));
  // The same with the major direction vertical.
  const QLineF transposed(QPointF(25.5, 0), QPointF(71, 45.5));
  checkStaysBetweenEndpoints(transposed);
  checkStaysBetweenEndpoints(QLineF(transposed.p2(), transposed.p1()));
}

BOOST_AUTO_TEST_CASE(points_of_a_whole_number_line) {
  const std::vector<QPoint> points = traverse(QLineF(0, 0, 4, 2));
  const std::vector<QPoint> expected{QPoint(0, 0), QPoint(1, 1), QPoint(2, 1), QPoint(3, 2), QPoint(4, 2)};
  BOOST_CHECK(points == expected);
}

BOOST_AUTO_TEST_SUITE_END()
