// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PropertySet.h>
#include <filters/output/FillColorProperty.h>
#include <zones/EditableSpline.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>

#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <utility>

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

// A copy owns its own properties: changing it leaves the original as it was.
BOOST_AUTO_TEST_CASE(test_property_set_copy_is_deep) {
  PropertySet original;
  original.locateOrCreate<output::FillColorProperty>()->setColor(Qt::white);

  PropertySet copy(original);
  copy.locate<output::FillColorProperty>()->setColor(Qt::black);
  BOOST_CHECK(original.locate<output::FillColorProperty>()->color() == QColor(Qt::white));

  PropertySet assigned;
  assigned = original;
  BOOST_CHECK(assigned.locate<output::FillColorProperty>() != original.locate<output::FillColorProperty>());
}

// Moving hands the properties over instead of copying them one by one.
BOOST_AUTO_TEST_CASE(test_property_set_move_keeps_the_properties) {
  PropertySet source;
  const auto property = source.locateOrCreate<output::FillColorProperty>();

  PropertySet moved(std::move(source));
  BOOST_CHECK(moved.locate<output::FillColorProperty>() == property);

  PropertySet assigned;
  assigned = std::move(moved);
  BOOST_CHECK(assigned.locate<output::FillColorProperty>() == property);
}

BOOST_AUTO_TEST_CASE(test_empty_spline_has_no_points) {
  const EditableSpline spline;
  BOOST_CHECK(spline.toPolygon().isEmpty());
}

BOOST_AUTO_TEST_CASE(test_set_bridged_takes_the_value) {
  EditableSpline spline;
  spline.appendVertex(QPointF(0, 0));
  spline.appendVertex(QPointF(10, 0));
  spline.appendVertex(QPointF(10, 10));
  spline.setBridged(true);
  BOOST_CHECK(spline.bridged());
  spline.setBridged(false);
  BOOST_CHECK(!spline.bridged());
}

BOOST_AUTO_TEST_CASE(test_vertex_has_at_least_zero_siblings) {
  EditableSpline spline;
  spline.appendVertex(QPointF(0, 0));
  spline.appendVertex(QPointF(10, 0));
  spline.appendVertex(QPointF(10, 10));
  const SplineVertex::Ptr vertex = spline.firstVertex();
  BOOST_CHECK(vertex->hasAtLeastSiblings(0));
  BOOST_CHECK(vertex->hasAtLeastSiblings(1));
  BOOST_CHECK(vertex->hasAtLeastSiblings(2));
  BOOST_CHECK(!vertex->hasAtLeastSiblings(3));
}

BOOST_AUTO_TEST_SUITE_END()
