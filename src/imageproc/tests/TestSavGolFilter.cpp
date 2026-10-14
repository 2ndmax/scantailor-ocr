// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <SavGolFilter.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <vector>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(SavGolFilterTestSuite)

BOOST_AUTO_TEST_CASE(test_central_area_is_rounded) {
  // Degree 0 is a plain mean: 15 / 25 = 0.6 in every 5x5 window, which rounds to 1.
  // The central area and the border areas must round the same way.
  std::vector<int> inp(9 * 9, 0);
  inp[4 * 9 + 4] = 15;

  const QImage result(savGolFilter(makeGrayImage(inp.data(), 9, 9), QSize(5, 5), 0, 0));
  for (int y = 0; y < 9; ++y) {
    for (int x = 0; x < 9; ++x) {
      BOOST_REQUIRE_EQUAL(qGray(result.pixel(x, y)), 1);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_vertical_ramp_is_kept) {
  // A polynomial of degree 1 passes through the filter unchanged, at the borders too.
  std::vector<int> inp(9 * 9);
  for (int y = 0; y < 9; ++y) {
    for (int x = 0; x < 9; ++x) {
      inp[y * 9 + x] = 20 * y;
    }
  }

  const QImage result(savGolFilter(makeGrayImage(inp.data(), 9, 9), QSize(5, 5), 0, 1));
  for (int y = 0; y < 9; ++y) {
    for (int x = 0; x < 9; ++x) {
      BOOST_REQUIRE_LE(std::abs(qGray(result.pixel(x, y)) - 20 * y), 1);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
