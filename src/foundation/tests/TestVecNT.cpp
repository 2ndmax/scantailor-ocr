// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <MatT.h>
#include <VecNT.h>
#include <VecT.h>

#include <boost/test/unit_test.hpp>
#include <type_traits>
#include <utility>

BOOST_AUTO_TEST_SUITE(FoundationVecNTTestSuite)

BOOST_AUTO_TEST_CASE(test_division_by_scalar) {
  const Vec2d vec(2, 8);
  const Vec2d res(vec / 2.0);
  BOOST_CHECK_CLOSE(res[0], 1.0, 1e-9);
  BOOST_CHECK_CLOSE(res[1], 4.0, 1e-9);
}

// Whole-number elements are divided one by one, not multiplied by 1 / scalar, which is 0 for them.
BOOST_AUTO_TEST_CASE(test_division_of_whole_numbers) {
  const VecNT<2, int> vec(10, 20);
  const VecNT<2, int> res(vec / 2);
  BOOST_CHECK_EQUAL(res[0], 5);
  BOOST_CHECK_EQUAL(res[1], 10);
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

BOOST_AUTO_TEST_CASE(test_mat_and_vec_move_semantics) {
  static_assert(std::is_nothrow_move_constructible_v<MatT<double>>);
  static_assert(std::is_nothrow_move_assignable_v<MatT<double>>);
  static_assert(std::is_nothrow_move_constructible_v<VecT<double>>);
  static_assert(std::is_nothrow_move_assignable_v<VecT<double>>);

  VecT<double> v1(4, 2.5);
  const double* vPtr = v1.data();
  VecT<double> v2(std::move(v1));
  BOOST_CHECK_EQUAL(v1.size(), 0u);
  BOOST_CHECK(v1.data() == nullptr);
  BOOST_CHECK_EQUAL(v2.size(), 4u);
  BOOST_CHECK(v2.data() == vPtr);

  VecT<double> v3;
  v3 = std::move(v2);
  BOOST_CHECK_EQUAL(v2.size(), 0u);
  BOOST_CHECK(v2.data() == nullptr);
  BOOST_CHECK_EQUAL(v3.size(), 4u);
  BOOST_CHECK(v3.data() == vPtr);
  BOOST_CHECK_EQUAL(v3[0], 2.5);

  MatT<double> m1(2, 3, 1.5);
  const double* mPtr = m1.data();
  MatT<double> m2(std::move(m1));
  BOOST_CHECK_EQUAL(m1.rows(), 0u);
  BOOST_CHECK_EQUAL(m1.cols(), 0u);
  BOOST_CHECK(m1.data() == nullptr);
  BOOST_CHECK_EQUAL(m2.rows(), 2u);
  BOOST_CHECK_EQUAL(m2.cols(), 3u);
  BOOST_CHECK(m2.data() == mPtr);

  MatT<double> m3;
  m3 = std::move(m2);
  BOOST_CHECK_EQUAL(m2.rows(), 0u);
  BOOST_CHECK_EQUAL(m2.cols(), 0u);
  BOOST_CHECK(m2.data() == nullptr);
  BOOST_CHECK_EQUAL(m3.rows(), 2u);
  BOOST_CHECK_EQUAL(m3.cols(), 3u);
  BOOST_CHECK(m3.data() == mPtr);
  BOOST_CHECK_EQUAL(m3(1, 2), 1.5);
}

BOOST_AUTO_TEST_SUITE_END()
