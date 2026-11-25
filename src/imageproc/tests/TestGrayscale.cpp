// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <Grayscale.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <limits>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(GrayscaleTestSuite)

BOOST_AUTO_TEST_CASE(test_null_image) {
  BOOST_CHECK(toGrayscale(QImage()).isNull());
}

BOOST_AUTO_TEST_CASE(test_mono_to_grayscale) {
  const int w = 50;
  const int h = 64;

  QImage mono(w, h, QImage::Format_Mono);
  QImage gray(w, h, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int rnd = rand() & 1;
      mono.setPixel(x, y, rnd ? 0 : 1);
      gray.setPixel(x, y, rnd ? 0 : 255);
    }
  }

  const QImage monoLsb(mono.convertToFormat(QImage::Format_MonoLSB));

  BOOST_REQUIRE(toGrayscale(mono) == gray);
  BOOST_CHECK(toGrayscale(monoLsb) == gray);
}

BOOST_AUTO_TEST_CASE(test_argb32_to_grayscale) {
  const int w = 50;
  const int h = 64;
  QImage argb32(w, h, QImage::Format_ARGB32);
  QImage gray(w, h, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int rnd = rand() & 1;
      argb32.setPixel(x, y, rnd ? 0x80303030 : 0x80606060);
      gray.setPixel(x, y, rnd ? 0x30 : 0x60);
    }
  }

  BOOST_CHECK(toGrayscale(argb32) == gray);
}

BOOST_AUTO_TEST_CASE(test_stretch_gray_range_clamps_extreme_and_nan_fractions) {
  GrayImage src(QSize(10, 10));
  for (int y = 0; y < 10; ++y) {
    uint8_t* line = src.data() + y * src.stride();
    for (int x = 0; x < 10; ++x) {
      line[x] = static_cast<uint8_t>(20 + x * 20);
    }
  }
  const GrayImage clippedAllBlack = stretchGrayRange(src, 300.0, 0.0);
  for (int y = 0; y < 10; ++y) {
    const uint8_t* line = clippedAllBlack.data() + y * clippedAllBlack.stride();
    for (int x = 0; x < 10; ++x) {
      BOOST_REQUIRE_EQUAL(int(line[x]), 0);
    }
  }

  BOOST_CHECK(stretchGrayRange(src, -1.0, 0.0) == stretchGrayRange(src, 0.0, 0.0));
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  BOOST_CHECK(stretchGrayRange(src, qnan, qnan) == stretchGrayRange(src, 0.0, 0.0));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc