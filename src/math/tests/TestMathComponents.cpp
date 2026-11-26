// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <HomographicTransform.h>
#include <ToLineProjector.h>
#include <XSpline.h>
#include <adiff/Function.h>
#include <spfit/ConstraintSet.h>
#include <spfit/FrenetFrame.h>
#include <spfit/LinearForceBalancer.h>
#include <spfit/Optimizer.h>
#include <spfit/PolylineModelShape.h>
#include <spfit/SplineFitter.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <vector>

namespace math {
namespace tests {
BOOST_AUTO_TEST_SUITE(MathComponentsTestSuite)

BOOST_AUTO_TEST_CASE(test_homographic_transform_1d_and_2d) {
  // 1D: f(x) = (2x + 1) / (0x + 1) = 2x + 1
  VecNT<4, double> m1d;
  m1d[0] = 2.0;
  m1d[1] = 0.0;
  m1d[2] = 1.0;
  m1d[3] = 1.0;
  const HomographicTransform<1, double> h1(m1d);
  BOOST_CHECK_CLOSE(h1(3.0), 7.0, 1e-9);
  BOOST_CHECK_CLOSE(h1.inv()(7.0), 3.0, 1e-9);

  // 2D translation (+10, -5) in column-major order
  VecNT<9, double> m2d;
  m2d[0] = 1.0;
  m2d[4] = 1.0;
  m2d[6] = 10.0;
  m2d[7] = -5.0;
  m2d[8] = 1.0;
  const HomographicTransform<2, double> h2(m2d);
  const Vec2d mapped = h2(Vec2d(2.0, 5.0));
  BOOST_CHECK_CLOSE(mapped[0], 12.0, 1e-9);
  BOOST_CHECK_SMALL(mapped[1], 1e-9);
  const Vec2d back = h2.inv()(mapped);
  BOOST_CHECK_CLOSE(back[0], 2.0, 1e-9);
  BOOST_CHECK_CLOSE(back[1], 5.0, 1e-9);
}

BOOST_AUTO_TEST_CASE(test_to_line_projector) {
  const ToLineProjector proj(QLineF(0.0, 0.0, 10.0, 0.0));
  BOOST_CHECK_CLOSE(proj.projectionScalar(QPointF(3.0, 4.0)), 0.3, 1e-9);
  BOOST_CHECK_CLOSE(proj.projectionDist(QPointF(3.0, 4.0)), 4.0, 1e-9);
  BOOST_CHECK_CLOSE(proj.projectionSqDist(QPointF(3.0, 4.0)), 16.0, 1e-9);
  BOOST_CHECK_CLOSE(proj.projectionPoint(QPointF(3.0, 4.0)).x(), 3.0, 1e-9);
  BOOST_CHECK_SMALL(proj.projectionPoint(QPointF(3.0, 4.0)).y(), 1e-9);

  // Zero-length line projects every point to p1().
  const ToLineProjector degenerate(QLineF(5.0, 7.0, 5.0, 7.0));
  BOOST_CHECK_CLOSE(degenerate.projectionPoint(QPointF(1.0, 2.0)).x(), 5.0, 1e-9);
  BOOST_CHECK_CLOSE(degenerate.projectionPoint(QPointF(1.0, 2.0)).y(), 7.0, 1e-9);
}

BOOST_AUTO_TEST_CASE(test_frenet_frame_and_force_balancer) {
  const spfit::FrenetFrame valid(Vec2d(1.0, 2.0), Vec2d(3.0, 0.0), spfit::FrenetFrame::Y_POINTS_DOWN);
  BOOST_REQUIRE(valid.isValid());
  BOOST_CHECK_CLOSE(valid.unitTangent()[0], 1.0, 1e-9);
  BOOST_CHECK_SMALL(valid.unitTangent()[1], 1e-9);
  BOOST_CHECK_SMALL(valid.unitNormal()[0], 1e-9);
  BOOST_CHECK_CLOSE(valid.unitNormal()[1], -1.0, 1e-9);

  const spfit::FrenetFrame up(Vec2d(1.0, 2.0), Vec2d(3.0, 0.0), spfit::FrenetFrame::Y_POINTS_UP);
  BOOST_REQUIRE(up.isValid());
  BOOST_CHECK_CLOSE(up.unitNormal()[1], 1.0, 1e-9);

  const spfit::FrenetFrame zero(Vec2d(0.0, 0.0), Vec2d(0.0, 0.0));
  BOOST_CHECK(!zero.isValid());

  spfit::LinearForceBalancer balancer(4.0);
  BOOST_CHECK_CLOSE(balancer.currentRatio(), 4.0, 1e-9);
  balancer.setTargetRatio(1.0);
  balancer.setIterationsToTarget(3);
  balancer.nextIteration();
  BOOST_CHECK_CLOSE(balancer.currentRatio(), 3.0, 1e-9);
  balancer.nextIteration();
  balancer.nextIteration();
  balancer.nextIteration();
  BOOST_CHECK_CLOSE(balancer.currentRatio(), 1.0, 1e-9);
  BOOST_CHECK(std::isfinite(balancer.calcInternalForceWeight(2.0, 10.0)));
}

BOOST_AUTO_TEST_CASE(test_optimizer_and_spline_fitter) {
  // Minimize (x - 3)^2 + (y - 5)^2 subject to x + y - 4 = 0 -> (1, 3), cost 8.
  spfit::Optimizer opt(2);
  QuadraticFunction force(2);
  force.A(0, 0) = 1.0;
  force.A(1, 1) = 1.0;
  force.b[0] = -6.0;
  force.b[1] = -10.0;
  force.c = 34.0;
  opt.addExternalForce(force);

  LinearFunction constraint(2);
  constraint.a[0] = 1.0;
  constraint.a[1] = 1.0;
  constraint.b = -4.0;
  opt.setConstraints({constraint});

  const spfit::OptimizationResult res = opt.optimize(0.0);
  BOOST_CHECK_CLOSE(res.forceBefore(), 34.0, 1e-9);
  BOOST_CHECK_CLOSE(res.forceAfter(), 8.0, 1e-9);
  BOOST_CHECK_CLOSE(opt.displacementVector()[0], 1.0, 1e-9);
  BOOST_CHECK_CLOSE(opt.displacementVector()[1], 3.0, 1e-9);

  opt.undoLastStep();
  BOOST_CHECK_SMALL(opt.displacementVector()[0], 1e-9);
  BOOST_CHECK_SMALL(opt.displacementVector()[1], 1e-9);

  XSpline spline;
  spline.appendControlPoint(QPointF(0.0, 4.0), 0.0);
  spline.appendControlPoint(QPointF(5.0, 6.0), 0.0);
  spline.appendControlPoint(QPointF(10.0, 4.0), 0.0);
  const spfit::PolylineModelShape target(std::vector<QPointF>{QPointF(0.0, 0.0), QPointF(10.0, 0.0)});
  spfit::SplineFitter fitter(&spline);
  spfit::ConstraintSet constraints(&spline);
  constraints.constrainControlPoint(0, QPointF(0.0, 0.0));
  constraints.constrainControlPoint(2, QPointF(10.0, 0.0));
  fitter.setConstraints(constraints);
  fitter.addAttractionForces(target);
  const spfit::OptimizationResult fitRes = fitter.optimize(0.0);
  BOOST_CHECK_LE(fitRes.forceAfter(), fitRes.forceBefore());
}

BOOST_AUTO_TEST_CASE(test_adiff_division) {
  adiff::SparseMap<2> sparseMap(2);
  sparseMap.markAllNonZero();

  const adiff::Function<2> x(0, 6.0, sparseMap);
  const adiff::Function<2> y(1, 3.0, sparseMap);
  const adiff::Function<2> q(x / y);
  const VecT<double> grad = q.gradient(sparseMap);
  const MatT<double> hess = q.hessian(sparseMap);

  BOOST_CHECK_CLOSE(q.value, 2.0, 1e-9);
  BOOST_CHECK_CLOSE(grad[0], 1.0 / 3.0, 1e-9);
  BOOST_CHECK_CLOSE(grad[1], -6.0 / 9.0, 1e-9);
  BOOST_CHECK_SMALL(hess(0, 0), 1e-9);
  BOOST_CHECK_CLOSE(hess(0, 1), -1.0 / 9.0, 1e-9);
  BOOST_CHECK_CLOSE(hess(1, 1), 12.0 / 27.0, 1e-9);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace math
