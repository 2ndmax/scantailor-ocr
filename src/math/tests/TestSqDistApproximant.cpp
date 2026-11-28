// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ToLineProjector.h>
#include <XSpline.h>
#include <spfit/FrenetFrame.h>
#include <spfit/PolylineModelShape.h>
#include <spfit/SqDistApproximant.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/tools/floating_point_comparison.hpp>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

namespace spfit {
namespace tests {
BOOST_AUTO_TEST_SUITE(SqDistApproximantTestSuite)

static const double PI = 3.14159265;

static double frand(double from, double to) {
  const double rand01 = rand() / double(RAND_MAX);
  return from + (to - from) * rand01;
}

BOOST_AUTO_TEST_CASE(test_point_distance) {
  for (int i = 0; i < 100; ++i) {
    const Vec2d origin(frand(-50, 50), frand(-50, 50));
    const SqDistApproximant approx(SqDistApproximant::pointDistance(origin));
    for (int j = 0; j < 10; ++j) {
      const Vec2d pt(frand(-50, 50), frand(-50, 50));
      const double control = (pt - origin).squaredNorm();
      BOOST_REQUIRE_CLOSE(approx.evaluate(pt), control, 1e-06);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_line_distance) {
  for (int i = 0; i < 100; ++i) {
    const Vec2d pt1(frand(-50, 50), frand(-50, 50));
    const double angle = frand(0, 2.0 * PI);
    const Vec2d delta(std::cos(angle), std::sin(angle));
    const QLineF line(pt1, pt1 + delta);
    const SqDistApproximant approx(SqDistApproximant::lineDistance(line));
    const ToLineProjector proj(line);
    for (int j = 0; j < 10; ++j) {
      const Vec2d pt(frand(-50, 50), frand(-50, 50));
      const double control = proj.projectionSqDist(pt);
      BOOST_REQUIRE_CLOSE(approx.evaluate(pt), control, 1e-06);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_general_case) {
  for (int i = 0; i < 100; ++i) {
    const Vec2d origin(frand(-50, 50), frand(-50, 50));
    const double angle = frand(0, 2.0 * PI);
    const Vec2d u(std::cos(angle), std::sin(angle));
    Vec2d v(-u[1], u[0]);
    if (rand() & 1) {
      v = -v;
    }
    const double m = frand(0, 3);
    const double n = frand(0, 3);

    const SqDistApproximant approx(origin, u, v, m, n);

    for (int j = 0; j < 10; ++j) {
      const Vec2d pt(frand(-50, 50), frand(-50, 50));
      const double uProj = u.dot(pt - origin);
      const double vProj = v.dot(pt - origin);
      const double control = m * uProj * uProj + n * vProj * vProj;
      BOOST_REQUIRE_CLOSE(approx.evaluate(pt), control, 1e-06);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_polyline_with_tiny_segment) {
  // Two vertices less than 0.001 px apart give a segment with a degenerate tangent.
  const PolylineModelShape shape(std::vector<QPointF>{QPointF(0, 0), QPointF(0.0005, 0), QPointF(10, 0)});
  const QPointF pt(0.00025, 5);
  const SqDistApproximant approx(shape.localSqDistApproximant(pt, FittableSpline::DEFAULT_SAMPLE));
  // The sample must still pull towards the polyline instead of silently not counting.
  BOOST_CHECK_CLOSE(approx.evaluate(pt), 25.0, 1.0);
}

BOOST_AUTO_TEST_CASE(test_curvature_without_tangent) {
  XSpline::PointAndDerivs pd;
  pd.firstDeriv = QPointF(0, 0);
  pd.secondDeriv = QPointF(1, 1);
  BOOST_CHECK_EQUAL(pd.signedCurvature(), 0.0);
  pd.firstDeriv = QPointF(1e-120, 0);
  BOOST_CHECK_EQUAL(pd.signedCurvature(), 0.0);
}

BOOST_AUTO_TEST_CASE(test_polyline_nonfinite_and_huge_points) {
  const PolylineModelShape shape(std::vector<QPointF>{QPointF(0, 0), QPointF(10, 0)});
  const SqDistApproximant nanApprox = shape.localSqDistApproximant(QPointF(std::numeric_limits<double>::quiet_NaN(), 0),
                                                                   FittableSpline::DEFAULT_SAMPLE);
  BOOST_CHECK_EQUAL(nanApprox.evaluate(QPointF(0, 0)), 0.0);

  const SqDistApproximant hugeApprox = shape.localSqDistApproximant(QPointF(1e200, 0), FittableSpline::DEFAULT_SAMPLE);
  BOOST_CHECK_EQUAL(hugeApprox.evaluate(QPointF(0, 0)), 0.0);
}

BOOST_AUTO_TEST_CASE(test_weighted_line_distance_preserves_weight_on_short_line) {
  const SqDistApproximant zeroLen = SqDistApproximant::weightedLineDistance(QLineF(1, 2, 1, 2), 5.0);
  BOOST_CHECK_CLOSE(zeroLen.evaluate(Vec2d(2, 2)), 5.0, 1e-9);

  const SqDistApproximant tinyLen = SqDistApproximant::weightedLineDistance(QLineF(1, 2, 1 + 1e-5, 2), 3.5);
  BOOST_CHECK_CLOSE(tinyLen.evaluate(Vec2d(1, 3)), 3.5, 1e-9);
}

BOOST_AUTO_TEST_CASE(test_weighted_curve_distance_with_invalid_frenet_frame) {
  const FrenetFrame invalidFrame(Vec2d(2.0, 3.0), Vec2d(0.0, 0.0));
  BOOST_REQUIRE(!invalidFrame.isValid());
  const SqDistApproximant approx(SqDistApproximant::weightedCurveDistance(Vec2d(5.0, 7.0), invalidFrame, 0.5, 4.0));
  BOOST_CHECK_CLOSE(approx.evaluate(Vec2d(5.0, 7.0)), 100.0, 1e-9);
  BOOST_CHECK_SMALL(approx.evaluate(Vec2d(2.0, 3.0)), 1e-9);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace spfit
