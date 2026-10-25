// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <LinearSolver.h>

#include <boost/test/tools/floating_point_comparison.hpp>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

namespace imageproc {
namespace tests {
BOOST_AUTO_TEST_SUITE(LinearSolverSuite)

// The matrices are stored column by column.

// [[2, 1], [1, 1]] * (1, -1) = (1, 0) with the first column scaled by 1e-8. Partial pivoting
// copes with that; skipping entries below an absolute threshold gave (5e7, 0).
BOOST_AUTO_TEST_CASE(column_scaled_system) {
  const double A[] = {2e-8, 1e-8, 1, 1};
  const double B[] = {1, 0};
  double X[2];
  LinearSolver(2, 2, 1).solve(A, X, B);
  BOOST_CHECK_CLOSE(X[0], 1e8, 1e-6);
  BOOST_CHECK_CLOSE(X[1], -1.0, 1e-6);
}

// A well conditioned system with small entries is not singular.
BOOST_AUTO_TEST_CASE(small_scaled_system) {
  const double A[] = {1e-9, 0, 0, 1e-9};
  const double B[] = {1e-9, 2e-9};
  double X[2];
  BOOST_REQUIRE_NO_THROW(LinearSolver(2, 2, 1).solve(A, X, B));
  BOOST_CHECK_CLOSE(X[0], 1.0, 1e-6);
  BOOST_CHECK_CLOSE(X[1], 2.0, 1e-6);
}

// A consistent overdetermined system with large values: the rounding errors exceed any small
// absolute threshold.
BOOST_AUTO_TEST_CASE(large_consistent_overdetermined_system) {
  const double a[3][2] = {{1.1e10, 0.3e10}, {0.7e10, 1.3e10}, {1.9e10, 0.2e10}};
  const double x[2] = {0.123, 0.456};
  const double A[] = {a[0][0], a[1][0], a[2][0], a[0][1], a[1][1], a[2][1]};
  double B[3];
  for (int r = 0; r < 3; ++r) {
    B[r] = a[r][0] * x[0] + a[r][1] * x[1];
  }
  double X[2];
  BOOST_REQUIRE_NO_THROW(LinearSolver(3, 2, 1).solve(A, X, B));
  BOOST_CHECK_CLOSE(X[0], x[0], 1e-6);
  BOOST_CHECK_CLOSE(X[1], x[1], 1e-6);
}

BOOST_AUTO_TEST_CASE(inconsistent_overdetermined_system_is_rejected) {
  const double A[] = {1, 0, 1, 0, 1, 1};
  const double B[] = {1, 2, 4};  // x + y would have to be 3.
  double X[2];
  BOOST_CHECK_THROW(LinearSolver(3, 2, 1).solve(A, X, B), std::runtime_error);
}

BOOST_AUTO_TEST_CASE(singular_system_is_rejected) {
  const double A[] = {1, 2, 2, 4};
  const double B[] = {1, 2};
  double X[2];
  BOOST_CHECK_THROW(LinearSolver(2, 2, 1).solve(A, X, B), std::runtime_error);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
