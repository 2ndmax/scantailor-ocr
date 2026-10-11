// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <CylindricalSurfaceDewarper.h>
#include <DistortionModel.h>
#include <WidthCorrection.h>

#include <QDomDocument>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace Tests {
using namespace dewarping;

BOOST_AUTO_TEST_SUITE(WidthCorrectionTestSuite)

namespace {
// Stretches the left part and compresses the right part.
WidthCorrection makeCorrection() {
  std::vector<double> samples;
  for (int i = 0; i <= 64; ++i) {
    const double x = i / 64.0;
    samples.push_back(x + 0.1 * std::sin(3.14159265358979 * x));
  }
  samples.back() = 1.0;  // sin(pi) is not exactly 0.
  return WidthCorrection(samples);
}

DistortionModel makeModel() {
  DistortionModel model;
  model.setTopCurve(Curve(std::vector<QPointF>{{100, 100}, {500, 80}, {900, 100}}));
  model.setBottomCurve(Curve(std::vector<QPointF>{{100, 1300}, {500, 1320}, {900, 1300}}));
  return model;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_null_is_identity) {
  const WidthCorrection correction;
  BOOST_CHECK(correction.isNull());
  for (const double x : {-0.5, 0.0, 0.3, 1.0, 1.5}) {
    BOOST_CHECK_EQUAL(correction.map(x), x);
    BOOST_CHECK_EQUAL(correction.unmap(x), x);
  }
}

BOOST_AUTO_TEST_CASE(test_invalid_samples_give_null) {
  BOOST_CHECK(WidthCorrection(std::vector<double>{0.0, 0.6, 0.5, 1.0}).isNull());
  BOOST_CHECK(WidthCorrection(std::vector<double>{0.1, 0.5, 1.0}).isNull());
  BOOST_CHECK(WidthCorrection(std::vector<double>{0.0, 0.5, 0.9}).isNull());
  BOOST_CHECK(WidthCorrection(std::vector<double>{0.0}).isNull());
  BOOST_CHECK(!WidthCorrection(std::vector<double>{0.0, 0.4, 1.0}).isNull());
}

BOOST_AUTO_TEST_CASE(test_map_and_unmap) {
  const WidthCorrection correction = makeCorrection();
  BOOST_REQUIRE(!correction.isNull());
  BOOST_CHECK_EQUAL(correction.map(0.0), 0.0);
  BOOST_CHECK_EQUAL(correction.map(1.0), 1.0);
  BOOST_CHECK_CLOSE(correction.map(0.5), 0.6, 1e-6);
  // Identity outside [0, 1].
  BOOST_CHECK_EQUAL(correction.map(-0.2), -0.2);
  BOOST_CHECK_EQUAL(correction.unmap(1.3), 1.3);
  for (int i = 1; i < 100; ++i) {
    const double x = i / 100.0;
    BOOST_CHECK_SMALL(correction.unmap(correction.map(x)) - x, 1e-9);
  }
}

BOOST_AUTO_TEST_CASE(test_xml_round_trip) {
  DistortionModel model = makeModel();
  BOOST_REQUIRE(model.isValid());
  QDomDocument doc;

  // Without a correction, nothing new is written.
  const QDomElement plain = model.toXml(doc, "distortion-model");
  BOOST_CHECK(plain.namedItem("width-correction").isNull());
  BOOST_CHECK(DistortionModel(plain).widthCorrection().isNull());

  model.setWidthCorrection(makeCorrection());
  const QDomElement el = model.toXml(doc, "distortion-model");
  BOOST_CHECK(!el.namedItem("width-correction").isNull());
  const DistortionModel loaded(el);
  BOOST_CHECK(loaded.matches(model));
  BOOST_CHECK(!loaded.matches(makeModel()));
  BOOST_CHECK_SMALL(loaded.widthCorrection().map(0.37) - model.widthCorrection().map(0.37), 1e-6);
}

BOOST_AUTO_TEST_CASE(test_changing_a_curve_discards_the_correction) {
  DistortionModel model = makeModel();
  model.setWidthCorrection(makeCorrection());
  model.setTopCurve(model.topCurve());
  BOOST_CHECK(model.widthCorrection().isNull());
}

BOOST_AUTO_TEST_CASE(test_dewarper_applies_the_correction) {
  const DistortionModel model = makeModel();
  const WidthCorrection correction = makeCorrection();
  const CylindricalSurfaceDewarper plain(model.topCurve().polyline(), model.bottomCurve().polyline(), 2.0);
  const CylindricalSurfaceDewarper corrected(model.topCurve().polyline(), model.bottomCurve().polyline(), 2.0,
                                             correction);
  for (const QPointF imgPt : {QPointF(200, 400), QPointF(500, 700), QPointF(850, 1200)}) {
    const QPointF a = plain.mapToDewarpedSpace(imgPt);
    const QPointF b = corrected.mapToDewarpedSpace(imgPt);
    BOOST_CHECK_SMALL(b.x() - correction.map(a.x()), 1e-9);
    BOOST_CHECK_SMALL(b.y() - a.y(), 1e-9);
    const QPointF back = corrected.mapToWarpedSpace(b);
    BOOST_CHECK_SMALL(back.x() - imgPt.x(), 1e-3);
    BOOST_CHECK_SMALL(back.y() - imgPt.y(), 1e-3);
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
