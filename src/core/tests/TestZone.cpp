// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PropertySet.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>

#include <QPolygonF>
#include <boost/test/unit_test.hpp>

namespace {
Zone zoneOf(const QPolygonF& polygon) {
  return Zone(SerializableSpline(polygon), PropertySet());
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ZoneTestSuite)

BOOST_AUTO_TEST_CASE(test_zone_with_area_is_valid) {
  BOOST_CHECK(zoneOf(QPolygonF(QRectF(10, 10, 30, 20))).isValid());
  QPolygonF triangle;
  triangle << QPointF(0, 0) << QPointF(10, 0) << QPointF(0, 10);
  BOOST_CHECK(zoneOf(triangle).isValid());
}

// A rectangle dragged straight up or down has four points but no area.
BOOST_AUTO_TEST_CASE(test_zone_without_area_is_invalid) {
  QPolygonF flat;
  flat << QPointF(10, 10) << QPointF(10, 10) << QPointF(10, 50) << QPointF(10, 50);
  BOOST_CHECK(!zoneOf(flat).isValid());

  QPolygonF collinear;
  collinear << QPointF(0, 0) << QPointF(10, 10) << QPointF(20, 20) << QPointF(30, 30);
  BOOST_CHECK(!zoneOf(collinear).isValid());

  BOOST_CHECK(!zoneOf(QPolygonF()).isValid());
}

BOOST_AUTO_TEST_SUITE_END()
