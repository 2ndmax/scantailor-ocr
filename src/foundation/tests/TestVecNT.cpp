// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <MatT.h>
#include <VecNT.h>
#include <VecT.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(FoundationVecNTTestSuite)

BOOST_AUTO_TEST_CASE(test_division_by_scalar) {
  const Vec2d vec(2, 8);
  const Vec2d res(vec / 2.0);
  BOOST_CHECK_CLOSE(res[0], 1.0, 1e-9);
  BOOST_CHECK_CLOSE(res[1], 4.0, 1e-9);
}

BOOST_AUTO_TEST_CASE(test_scalar_divided_by_vector) {
  const Vec2d vec(2, 8);
  const Vec2d res(16.0 / vec);
  BOOST_CHECK_CLOSE(res[0], 8.0, 1e-9);
  BOOST_CHECK_CLOSE(res[1], 2.0, 1e-9);
}

BOOST_AUTO_TEST_CASE(test_conversion_to_another_element_type) {
  VecT<double> vec(2);
  vec[0] = 1.5;
  vec[1] = -2.5;
  const VecT<float> vecF(vec);
  BOOST_REQUIRE_EQUAL(vecF.size(), 2u);
  BOOST_CHECK_EQUAL(vecF[0], 1.5f);
  BOOST_CHECK_EQUAL(vecF[1], -2.5f);

  MatT<double> mat(1, 2);
  mat(0, 0) = 0.5;
  mat(0, 1) = 3.0;
  const MatT<float> matF(mat);
  BOOST_REQUIRE_EQUAL(matF.rows(), 1u);
  BOOST_REQUIRE_EQUAL(matF.cols(), 2u);
  BOOST_CHECK_EQUAL(matF(0, 0), 0.5f);
  BOOST_CHECK_EQUAL(matF(0, 1), 3.0f);
}

BOOST_AUTO_TEST_SUITE_END()
