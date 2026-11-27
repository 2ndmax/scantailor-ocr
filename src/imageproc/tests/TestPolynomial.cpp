// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <GrayImage.h>
#include <PolynomialLine.h>
#include <PolynomialSurface.h>

#include <QSize>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(PolynomialTestSuite)

BOOST_AUTO_TEST_CASE(test_polynomial_line_fit_and_nan_fallback) {
  const std::vector<uint8_t> linear{10, 30, 50, 70, 90};
  const PolynomialLine poly(1, linear.data(), static_cast<int>(linear.size()), 1);
  std::vector<uint8_t> fitted(5, 0);
  poly.output(fitted.data(), static_cast<int>(fitted.size()), 1);
  for (size_t i = 0; i < linear.size(); ++i) {
    BOOST_CHECK_EQUAL(fitted[i], linear[i]);
  }

  // Non-finite input triggers LinearSolver runtime_error and falls back to the mean of finite values.
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::vector<double> noisy{100.0, qnan, 200.0};
  const PolynomialLine fallbackPoly(2, noisy.data(), static_cast<int>(noisy.size()), 1);
  std::vector<uint8_t> out(3, 0);
  fallbackPoly.output(out.data(), static_cast<int>(out.size()), 1);
  BOOST_CHECK_EQUAL(out[0], 150);
  BOOST_CHECK_EQUAL(out[1], 150);
  BOOST_CHECK_EQUAL(out[2], 150);

  // Non-finite inputs keep RoundAndClipPostProcessor bounded without UB.
  const std::vector<double> badOut{qnan, inf, -inf};
  const PolynomialLine nanCheck(0, badOut.data(), 1, 1);
  std::vector<uint8_t> nanOut(1, 42);
  nanCheck.output(nanOut.data(), 1, 1);
  BOOST_CHECK_EQUAL(nanOut[0], 0);

  const std::vector<double> extremeOut{-1e20, 1e20};
  const PolynomialLine clippedPoly(1, extremeOut.data(), static_cast<int>(extremeOut.size()), 1);
  std::vector<uint8_t> clippedOut(2, 128);
  clippedPoly.output(clippedOut.data(), 2, 1);
  BOOST_CHECK_EQUAL(clippedOut[0], 0);
  BOOST_CHECK_EQUAL(clippedOut[1], 255);
}

BOOST_AUTO_TEST_CASE(test_polynomial_surface_unmasked_masked_and_empty) {
  GrayImage src(QSize(16, 16));
  for (int y = 0; y < 16; ++y) {
    uint8_t* line = src.data() + y * src.stride();
    for (int x = 0; x < 16; ++x) {
      line[x] = static_cast<uint8_t>(100 + x * 4 + y * 2);
    }
  }

  const PolynomialSurface surf(2, 2, src);
  const GrayImage rendered = surf.render(QSize(16, 16));
  BOOST_REQUIRE(rendered.size() == QSize(16, 16));
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      const int diff = std::abs(static_cast<int>(rendered.data()[y * rendered.stride() + x])
                                - static_cast<int>(src.data()[y * src.stride() + x]));
      BOOST_CHECK_LE(diff, 2);
    }
  }

  BinaryImage halfMask(16, 16, WHITE);
  halfMask.fill(QRect(0, 0, 16, 8), BLACK);
  const PolynomialSurface masked(1, 1, src, halfMask);
  const GrayImage maskedOut = masked.render(QSize(16, 16));
  BOOST_CHECK_CLOSE(static_cast<double>(maskedOut.data()[0]), 100.0, 2.5);

  const BinaryImage emptyMask(16, 16, WHITE);
  const PolynomialSurface emptySurf(2, 2, src, emptyMask);
  BOOST_CHECK(emptySurf.render(QSize(8, 8)).size() == QSize(8, 8));
  BOOST_CHECK(surf.render(QSize(0, 8)).isNull());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
