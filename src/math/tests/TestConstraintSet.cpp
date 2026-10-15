// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XSpline.h>
#include <spfit/ConstraintSet.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/unit_test.hpp>
#include <cmath>

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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace spfit
