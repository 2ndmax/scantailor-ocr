// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <interaction/RectHandleGeometry.h>

#include <boost/test/unit_test.hpp>

namespace {
const QRectF RECT(QPointF(10, 20), QPointF(110, 220));
const QSizeF MIN_SIZE(15, 25);
}  // namespace

BOOST_AUTO_TEST_SUITE(RectHandleGeometryTestSuite)

BOOST_AUTO_TEST_CASE(test_corners) {
  BOOST_CHECK(rectCorner(RECT, RECT_TOP | RECT_LEFT) == QPointF(10, 20));
  BOOST_CHECK(rectCorner(RECT, RECT_TOP | RECT_RIGHT) == QPointF(110, 20));
  BOOST_CHECK(rectCorner(RECT, RECT_BOTTOM | RECT_LEFT) == QPointF(10, 220));
  BOOST_CHECK(rectCorner(RECT, RECT_BOTTOM | RECT_RIGHT) == QPointF(110, 220));
}

BOOST_AUTO_TEST_CASE(test_edges) {
  BOOST_CHECK(rectEdge(RECT, RECT_TOP) == QLineF(10, 20, 110, 20));
  BOOST_CHECK(rectEdge(RECT, RECT_BOTTOM) == QLineF(10, 220, 110, 220));
  BOOST_CHECK(rectEdge(RECT, RECT_LEFT) == QLineF(10, 20, 10, 220));
  BOOST_CHECK(rectEdge(RECT, RECT_RIGHT) == QLineF(110, 20, 110, 220));
}

BOOST_AUTO_TEST_CASE(test_moving_a_corner_moves_its_two_edges) {
  BOOST_CHECK(resizeRectByEdges(RECT, RECT_TOP | RECT_LEFT, QPointF(5, 6), MIN_SIZE)
              == QRectF(QPointF(5, 6), QPointF(110, 220)));
  BOOST_CHECK(resizeRectByEdges(RECT, RECT_BOTTOM | RECT_RIGHT, QPointF(120, 230), MIN_SIZE)
              == QRectF(QPointF(10, 20), QPointF(120, 230)));
}

BOOST_AUTO_TEST_CASE(test_moving_an_edge_leaves_the_other_direction) {
  BOOST_CHECK(resizeRectByEdges(RECT, RECT_TOP, QPointF(500, 6), MIN_SIZE)
              == QRectF(QPointF(10, 6), QPointF(110, 220)));
  BOOST_CHECK(resizeRectByEdges(RECT, RECT_RIGHT, QPointF(120, 500), MIN_SIZE)
              == QRectF(QPointF(10, 20), QPointF(120, 220)));
}

// Dragged past the opposite edge, the rectangle stops at its minimum size in every direction.
BOOST_AUTO_TEST_CASE(test_minimum_size_in_each_direction) {
  BOOST_CHECK_EQUAL(resizeRectByEdges(RECT, RECT_TOP, QPointF(0, 1000), MIN_SIZE).top(), 195.0);
  BOOST_CHECK_EQUAL(resizeRectByEdges(RECT, RECT_BOTTOM, QPointF(0, -1000), MIN_SIZE).bottom(), 45.0);
  BOOST_CHECK_EQUAL(resizeRectByEdges(RECT, RECT_LEFT, QPointF(1000, 0), MIN_SIZE).left(), 95.0);
  BOOST_CHECK_EQUAL(resizeRectByEdges(RECT, RECT_RIGHT, QPointF(-1000, 0), MIN_SIZE).right(), 25.0);
}

BOOST_AUTO_TEST_SUITE_END()
