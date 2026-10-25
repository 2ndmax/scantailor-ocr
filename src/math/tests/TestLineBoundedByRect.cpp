// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <LineBoundedByRect.h>

#include <QLineF>
#include <QRectF>
#include <algorithm>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(MathLineBoundedByRectTestSuite)

BOOST_AUTO_TEST_CASE(test_line_through_rect_center) {
  const QRectF rect(0, 0, 100, 100);
  QLineF line(-10, 50, 110, 50);
  const bool ok = lineBoundedByRect(line, rect);
  BOOST_REQUIRE(ok);
  BOOST_CHECK_SMALL(line.p1().x(), 1e-9);
  BOOST_CHECK_CLOSE(line.p1().y(), 50.0, 0.01);
  BOOST_CHECK_CLOSE(line.p2().x(), 100.0, 0.01);
  BOOST_CHECK_CLOSE(line.p2().y(), 50.0, 0.01);
}

BOOST_AUTO_TEST_CASE(test_line_through_rect_diagonal) {
  const QRectF rect(0, 0, 100, 100);
  QLineF line(-5, -5, 105, 105);
  const bool ok = lineBoundedByRect(line, rect);
  BOOST_REQUIRE(ok);
  BOOST_CHECK_SMALL(line.p1().x(), 1e-10);
  BOOST_CHECK_SMALL(line.p1().y(), 1e-10);
  BOOST_CHECK_CLOSE(line.p2().x(), 100.0, 0.01);
  BOOST_CHECK_CLOSE(line.p2().y(), 100.0, 0.01);
}

BOOST_AUTO_TEST_CASE(test_line_outside_rect_no_intersection) {
  const QRectF rect(10, 10, 80, 80);
  QLineF line(5, 0, 5, 100);
  const bool ok = lineBoundedByRect(line, rect);
  BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(test_vertical_line_through_rect) {
  const QRectF rect(10, 10, 80, 80);
  QLineF line(50, 0, 50, 200);
  const bool ok = lineBoundedByRect(line, rect);
  BOOST_REQUIRE(ok);
  BOOST_CHECK_CLOSE(line.p1().x(), 50.0, 0.01);
  BOOST_CHECK_CLOSE(line.p1().y(), 10.0, 0.01);
  BOOST_CHECK_CLOSE(line.p2().x(), 50.0, 0.01);
  BOOST_CHECK_CLOSE(line.p2().y(), 90.0, 0.01);
}

// A line through a corner meets both edges there; rounding may put both intersections slightly
// outside their edges, and then the line was lost.
BOOST_AUTO_TEST_CASE(test_lines_through_a_corner) {
  const QRectF rect(0.3, 0.7, 123.456, 78.9);
  int lost = 0;
  for (int i = 1; i < 100; ++i) {
    for (int j = 1; j < 100; ++j) {
      const QPointF inner(rect.left() + rect.width() * i / 100.0, rect.top() + rect.height() * j / 100.0);
      QLineF line(inner, rect.bottomRight());
      if (!lineBoundedByRect(line, rect)) {
        ++lost;
        continue;
      }
      // One end is the corner, the other one is on the boundary.
      const double toCorner
          = std::min(QLineF(line.p1(), rect.bottomRight()).length(), QLineF(line.p2(), rect.bottomRight()).length());
      BOOST_CHECK_SMALL(toCorner, 1e-6);
    }
  }
  BOOST_CHECK_EQUAL(lost, 0);
}

BOOST_AUTO_TEST_SUITE_END()
