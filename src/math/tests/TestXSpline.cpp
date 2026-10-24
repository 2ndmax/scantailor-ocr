// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XSpline.h>

#include <boost/test/unit_test.hpp>
#include <cmath>
#include <limits>
#include <vector>

namespace tests {
namespace {
bool near(const QPointF& a, const QPointF& b) {
  return std::abs(a.x() - b.x()) < 1e-6 && std::abs(a.y() - b.y()) < 1e-6;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(XSplineTestSuite)

BOOST_AUTO_TEST_CASE(test_sampling_ends_with_nan_coordinates) {
  // A damaged project file can hold NaN in a curve; sampling used to recurse without end.
  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 1);
  spline.appendControlPoint(QPointF(std::numeric_limits<double>::quiet_NaN(), 0), 1);
  spline.appendControlPoint(QPointF(100, 0), 1);
  const std::vector<QPointF> polyline = spline.toPolyline();
  BOOST_TEST(polyline.size() >= 2);
}

BOOST_AUTO_TEST_CASE(test_sampling_ends_with_huge_coordinates) {
  // Neighbouring t values never give points close to each other here.
  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 1);
  spline.appendControlPoint(QPointF(1e300, -1e300), 1);
  spline.appendControlPoint(QPointF(-1e300, 1e300), 1);
  const std::vector<QPointF> polyline = spline.toPolyline();
  BOOST_TEST(polyline.size() >= 2);
}

BOOST_AUTO_TEST_CASE(test_t_just_outside_the_range) {
  // Rounding can give a t just outside [0, 1]; it counts as the end of the curve.
  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 0);
  spline.appendControlPoint(QPointF(10, 0), 0);
  spline.appendControlPoint(QPointF(20, 5), 0);
  BOOST_TEST(near(spline.pointAt(1.0 + 1e-12), spline.pointAt(1.0)));
  BOOST_TEST(near(spline.pointAt(-1e-12), spline.pointAt(0.0)));

  std::vector<XSpline::LinearCoefficient> coeffs;
  spline.linearCombinationAt(1.0 + 1e-12, coeffs);
  for (const XSpline::LinearCoefficient& c : coeffs) {
    BOOST_TEST(c.controlPointIdx >= 0);
    BOOST_TEST(c.controlPointIdx < spline.numControlPoints());
  }
  BOOST_TEST(near(spline.pointAndDtsAt(1.0 + 1e-12).point, spline.pointAt(1.0)));
}

BOOST_AUTO_TEST_CASE(test_single_control_point) {
  XSpline spline;
  spline.appendControlPoint(QPointF(5, 7), 0);
  BOOST_TEST(near(spline.pointAt(0.5), QPointF(5, 7)));

  std::vector<XSpline::LinearCoefficient> coeffs;
  spline.linearCombinationAt(0.5, coeffs);
  BOOST_REQUIRE(coeffs.size() == 1);
  BOOST_TEST(coeffs[0].controlPointIdx == 0);
  BOOST_TEST(coeffs[0].coeff == 1.0);

  const XSpline::PointAndDerivs pd = spline.pointAndDtsAt(0.5);
  BOOST_TEST(near(pd.point, QPointF(5, 7)));
  BOOST_TEST(near(pd.firstDeriv, QPointF(0, 0)));
  BOOST_TEST(near(pd.secondDeriv, QPointF(0, 0)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
