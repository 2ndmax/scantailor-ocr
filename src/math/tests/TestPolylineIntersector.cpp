// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PolylineIntersector.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <vector>

BOOST_AUTO_TEST_SUITE(PolylineIntersectorTestSuite)

// A polyline needs at least one segment; fewer points would make the search read outside the vector.
BOOST_AUTO_TEST_CASE(test_fewer_than_two_points_are_rejected) {
  const std::vector<QPointF> empty;
  const std::vector<QPointF> single{QPointF(1, 1)};
  BOOST_CHECK_THROW(PolylineIntersector{empty}, std::invalid_argument);
  BOOST_CHECK_THROW(PolylineIntersector{single}, std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_two_points_make_a_polyline) {
  const PolylineIntersector intersector(std::vector<QPointF>{QPointF(0, 0), QPointF(10, 0)});
  PolylineIntersector::Hint hint;
  const QPointF pt = intersector.intersect(QLineF(QPointF(4, -5), QPointF(4, 5)), hint);
  BOOST_CHECK_CLOSE(pt.x(), 4.0, 1e-9);
  BOOST_CHECK_SMALL(pt.y(), 1e-9);
}

BOOST_AUTO_TEST_SUITE_END()
