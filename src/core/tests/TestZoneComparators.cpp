// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PropertySet.h>
#include <filters/output/FillColorProperty.h>
#include <filters/output/FillZoneComparator.h>
#include <filters/output/PictureLayerProperty.h>
#include <filters/output/PictureZoneComparator.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>
#include <zones/ZoneSet.h>

#include <QColor>
#include <QPolygonF>
#include <QRectF>
#include <boost/test/unit_test.hpp>

namespace Tests {
namespace {
const QPolygonF RECT = QPolygonF(QRectF(10, 20, 30, 40));
// A difference well below one 1/4096 rounding step of PolygonUtils::fuzzyCompare().
const QPolygonF RECT_WITH_ROUNDING = QPolygonF(QRectF(10.0001, 20.0001, 30.0001, 40.0001));

Zone zone(const QPolygonF& polygon, const PropertySet& props = PropertySet()) {
  return Zone(SerializableSpline(polygon), props);
}

PropertySet fillProperties(const QColor& color) {
  PropertySet props;
  props.locateOrCreate<output::FillColorProperty>()->setColor(color);
  return props;
}

PropertySet pictureProperties(const output::PictureLayerProperty::Layer layer) {
  PropertySet props;
  props.locateOrCreate<output::PictureLayerProperty>()->setLayer(layer);
  return props;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ZoneComparatorTestSuite)

BOOST_AUTO_TEST_CASE(fill_zone_comparator_compares_sets_zones_and_properties) {
  const PropertySet white = fillProperties(Qt::white);
  const PropertySet black = fillProperties(Qt::black);
  const Zone one = zone(RECT, white);

  ZoneSet lhs;
  lhs.add(one);
  ZoneSet same;
  same.add(zone(RECT, white));
  BOOST_TEST(output::FillZoneComparator::equal(lhs, same));
  BOOST_TEST(output::FillZoneComparator::equal(same, lhs));

  ZoneSet longer = same;
  longer.add(zone(RECT, white));
  BOOST_TEST(!output::FillZoneComparator::equal(lhs, longer));
  BOOST_TEST(!output::FillZoneComparator::equal(longer, lhs));
  BOOST_TEST(!output::FillZoneComparator::equal(ZoneSet(), lhs));

  ZoneSet otherProperty;
  otherProperty.add(zone(RECT, black));
  BOOST_TEST(!output::FillZoneComparator::equal(lhs, otherProperty));

  ZoneSet rounded;
  rounded.add(zone(RECT_WITH_ROUNDING, white));
  BOOST_TEST(output::FillZoneComparator::equal(lhs, rounded));
  BOOST_TEST(output::FillZoneComparator::equal(one, zone(RECT_WITH_ROUNDING, white)));
  BOOST_TEST(!output::FillZoneComparator::equal(one, zone(RECT, black)));
  BOOST_TEST(output::FillZoneComparator::equal(white, white));
  BOOST_TEST(!output::FillZoneComparator::equal(white, black));
}

BOOST_AUTO_TEST_CASE(picture_zone_comparator_compares_sets_zones_and_properties) {
  using Layer = output::PictureLayerProperty::Layer;
  const PropertySet noOp = pictureProperties(Layer::ZONENOOP);
  const PropertySet painter = pictureProperties(Layer::ZONEPAINTER2);
  const Zone one = zone(RECT, noOp);

  ZoneSet lhs;
  lhs.add(one);
  ZoneSet same;
  same.add(zone(RECT, noOp));
  BOOST_TEST(output::PictureZoneComparator::equal(lhs, same));
  BOOST_TEST(output::PictureZoneComparator::equal(same, lhs));

  ZoneSet longer = same;
  longer.add(zone(RECT, noOp));
  BOOST_TEST(!output::PictureZoneComparator::equal(lhs, longer));
  BOOST_TEST(!output::PictureZoneComparator::equal(longer, lhs));
  BOOST_TEST(!output::PictureZoneComparator::equal(ZoneSet(), lhs));

  ZoneSet otherProperty;
  otherProperty.add(zone(RECT, painter));
  BOOST_TEST(!output::PictureZoneComparator::equal(lhs, otherProperty));

  ZoneSet rounded;
  rounded.add(zone(RECT_WITH_ROUNDING, noOp));
  BOOST_TEST(output::PictureZoneComparator::equal(lhs, rounded));
  BOOST_TEST(output::PictureZoneComparator::equal(one, zone(RECT_WITH_ROUNDING, noOp)));
  BOOST_TEST(!output::PictureZoneComparator::equal(one, zone(RECT, painter)));
  BOOST_TEST(output::PictureZoneComparator::equal(noOp, noOp));
  BOOST_TEST(!output::PictureZoneComparator::equal(noOp, painter));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
