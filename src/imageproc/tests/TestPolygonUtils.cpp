// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PolygonUtils.h>

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(PolygonUtilsTestSuite)

// The hull of a square away from the origin holds its corners only, closed, without
// extra points at (0, 0).
BOOST_AUTO_TEST_CASE(convex_hull_has_no_extra_points) {
  const std::vector<QPointF> square
      = {QPointF(10, 10), QPointF(20, 10), QPointF(20, 20), QPointF(10, 20), QPointF(15, 15)};
  const QPolygonF hull = PolygonUtils::convexHull(square);
  BOOST_CHECK_EQUAL(hull.size(), 5);
  BOOST_CHECK(!hull.contains(QPointF(0, 0)));
  BOOST_CHECK(!hull.contains(QPointF(15, 15)));
  BOOST_CHECK(hull.boundingRect() == QRectF(10, 10, 10, 10));
}

BOOST_AUTO_TEST_CASE(fuzzy_compare_empty_and_single_point) {
  const QPolygonF empty;
  QPolygonF ptA;
  ptA << QPointF(1.0, 2.0);
  QPolygonF ptANear;
  ptANear << QPointF(1.0001, 2.0001);
  QPolygonF ptB;
  ptB << QPointF(5.0, 6.0);
  QPolygonF twoPts;
  twoPts << QPointF(1.0, 2.0) << QPointF(3.0, 4.0);
  QPolygonF degenerate;
  degenerate << QPointF(5.0, 6.0) << QPointF(5.0, 6.0);

  BOOST_CHECK(PolygonUtils::fuzzyCompare(empty, empty));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(empty, ptA));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(ptA, empty));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(empty, twoPts));
  BOOST_CHECK(PolygonUtils::fuzzyCompare(ptA, ptA));
  BOOST_CHECK(PolygonUtils::fuzzyCompare(ptA, ptANear));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(ptA, ptB));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(ptA, twoPts));
  BOOST_CHECK(!PolygonUtils::fuzzyCompare(ptA, degenerate));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
