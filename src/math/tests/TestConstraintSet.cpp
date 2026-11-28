// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XSpline.h>
#include <spfit/ConstraintSet.h>
#include <spfit/Optimizer.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <stdexcept>

namespace spfit {
namespace tests {
BOOST_AUTO_TEST_SUITE(ConstraintSetTestSuite)

BOOST_AUTO_TEST_CASE(test_control_point_on_line) {
  // The constraint is a function of control point displacements: it has to be zero
  // for every displacement that moves the control point onto the line.
  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 0);
  spline.appendControlPoint(QPointF(3, 5), 0);

  ConstraintSet constraints(&spline);
  constraints.constrainControlPoint(1, QLineF(QPointF(0, 0), QPointF(2, 2)));
  BOOST_REQUIRE_EQUAL(constraints.constraints().size(), 1u);
  const LinearFunction& f = constraints.constraints().front();

  // (3, 5) moved to (4, 4) and to (5, 5).
  const double toPoint44[] = {0, 0, 1, -1};
  const double toPoint55[] = {0, 0, 2, 0};
  BOOST_CHECK_SMALL(f.evaluate(toPoint44), 1e-9);
  BOOST_CHECK_SMALL(f.evaluate(toPoint55), 1e-9);

  // No displacement leaves (3, 5) off the line.
  const double none[] = {0, 0, 0, 0};
  BOOST_CHECK(std::abs(f.evaluate(none)) > 1.0);
}

BOOST_AUTO_TEST_CASE(test_constraint_set_and_optimizer_validate_bounds) {
  BOOST_CHECK_THROW(ConstraintSet(nullptr), std::invalid_argument);

  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 0);
  spline.appendControlPoint(QPointF(10, 0), 0);
  ConstraintSet constraints(&spline);
  BOOST_CHECK_THROW(constraints.constrainControlPoint(-1, QPointF(0, 0)), std::out_of_range);
  BOOST_CHECK_THROW(constraints.constrainControlPoint(2, QLineF(0, 0, 1, 1)), std::out_of_range);

  Optimizer opt(4);
  std::list<LinearFunction> badConstraints;
  badConstraints.emplace_back(2);
  BOOST_CHECK_THROW(opt.setConstraints(badConstraints), std::invalid_argument);

  BOOST_CHECK_THROW(opt.addExternalForce(QuadraticFunction(2)), std::invalid_argument);
  BOOST_CHECK_THROW(opt.addInternalForce(QuadraticFunction(2)), std::invalid_argument);
  BOOST_CHECK_THROW(opt.addExternalForce(QuadraticFunction(2), std::vector<int>{0}), std::invalid_argument);
  BOOST_CHECK_THROW(opt.addExternalForce(QuadraticFunction(2), std::vector<int>{0, 4}), std::out_of_range);
  BOOST_CHECK_THROW(opt.addInternalForce(QuadraticFunction(2), std::vector<int>{-1, 0}), std::out_of_range);
}

BOOST_AUTO_TEST_CASE(test_short_line_constraint_is_normalized_and_degenerate_falls_back) {
  XSpline spline;
  spline.appendControlPoint(QPointF(0, 0), 0);
  spline.appendControlPoint(QPointF(3, 5), 0);

  ConstraintSet constraints(&spline);
  constraints.constrainControlPoint(1, QLineF(QPointF(0, 0), QPointF(1e-8, 1e-8)));
  BOOST_REQUIRE_EQUAL(constraints.constraints().size(), 1u);
  const LinearFunction& f = constraints.constraints().front();
  BOOST_CHECK_CLOSE(std::hypot(f.a[2], f.a[3]), 1.0, 1e-9);
  const double toPoint44[] = {0, 0, 1, -1};
  const double none[] = {0, 0, 0, 0};
  BOOST_CHECK_SMALL(f.evaluate(toPoint44), 1e-9);
  BOOST_CHECK_CLOSE(std::abs(f.evaluate(none)), std::sqrt(2.0), 1e-9);

  ConstraintSet splineConstraints(&spline);
  splineConstraints.constrainSplinePoint(0.5, QLineF(QPointF(1, 0), QPointF(1.0 + 1e-8, 0)));
  BOOST_REQUIRE_EQUAL(splineConstraints.constraints().size(), 1u);
  const LinearFunction& sf = splineConstraints.constraints().front();
  // Midpoint at t=0.5 is (1.5, 2.5); shifting both control points by dy=-2.5 moves it onto y=0.
  const double toYZero[] = {0, -2.5, 0, -2.5};
  BOOST_CHECK_SMALL(sf.evaluate(toYZero), 1e-9);
  BOOST_CHECK_CLOSE(std::abs(sf.evaluate(none)), 2.5, 1e-9);

  ConstraintSet fallback(&spline);
  fallback.constrainControlPoint(1, QLineF(QPointF(1, 2), QPointF(1, 2)));
  BOOST_CHECK_EQUAL(fallback.constraints().size(), 2u);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace spfit
