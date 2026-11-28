// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BWColor.h>
#include <BinaryImage.h>
#include <GrayImage.h>
#include <Grayscale.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <random>

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
  mono.setColorCount(2);
  mono.setColor(0, 0xff000000);
  mono.setColor(1, 0xffffffff);
  QImage gray(w, h, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());

  std::mt19937 rng(0x5CA1E002u);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const unsigned rnd = rng() & 1u;
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

  std::mt19937 rng(0x5CA1E003u);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const unsigned rnd = rng() & 1u;
      argb32.setPixel(x, y, rnd ? 0x80303030 : 0x80606060);
      gray.setPixel(x, y, rnd ? 0x30 : 0x60);
    }
  }

  BOOST_CHECK(toGrayscale(argb32) == gray);
}

BOOST_AUTO_TEST_CASE(test_rgb888_to_grayscale) {
  const int w = 53;
  const int h = 41;
  QImage rgb888(w, h, QImage::Format_RGB888);
  QImage gray(w, h, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int r = (x * 7 + y * 13) & 0xff;
      const int g = (x * 19 + y * 5) & 0xff;
      const int b = (x * 11 + y * 23) & 0xff;
      rgb888.setPixel(x, y, qRgb(r, g, b));
      gray.setPixel(x, y, qGray(r, g, b));
    }
  }

  BOOST_CHECK(toGrayscale(rgb888) == gray);
}

BOOST_AUTO_TEST_CASE(test_indexed8_non_identity_grayscale_palettes) {
  // Two-entry grayscale palette: index 1 means white (255), not gray level 1.
  {
    QImage twoTone(4, 1, QImage::Format_Indexed8);
    twoTone.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
    twoTone.setPixel(0, 0, 0);
    twoTone.setPixel(1, 0, 1);
    twoTone.setPixel(2, 0, 0);
    twoTone.setPixel(3, 0, 1);

    const QImage gray = toGrayscale(twoTone);
    BOOST_REQUIRE_EQUAL(gray.colorCount(), 256);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(0, 0)), 0);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(1, 0)), 255);
  }

  // Inverted 256-entry grayscale palette: index i maps to 255 - i.
  {
    QImage inverted(4, 1, QImage::Format_Indexed8);
    QVector<QRgb> table(256);
    for (int i = 0; i < 256; ++i) {
      table[i] = qRgb(255 - i, 255 - i, 255 - i);
    }
    inverted.setColorTable(table);
    inverted.setPixel(0, 0, 10);
    inverted.setPixel(1, 0, 200);
    inverted.setPixel(2, 0, 0);
    inverted.setPixel(3, 0, 255);

    const QImage gray = toGrayscale(inverted);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(0, 0)), 245);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(1, 0)), 55);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(2, 0)), 255);
    BOOST_CHECK_EQUAL(qGray(gray.pixel(3, 0)), 0);
  }

  // Standard 256-entry identity palette is preserved as-is.
  {
    QImage identity(4, 1, QImage::Format_Indexed8);
    identity.setColorTable(createGrayscalePalette());
    identity.setPixel(0, 0, 0);
    identity.setPixel(1, 0, 42);
    identity.setPixel(2, 0, 180);
    identity.setPixel(3, 0, 255);

    const QImage gray = toGrayscale(identity);
    BOOST_CHECK(gray == identity);
  }
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

BOOST_AUTO_TEST_CASE(test_mono_histogram_accumulates_identical_palette_grays) {
  QImage mono(4, 2, QImage::Format_Mono);
  mono.setColorCount(2);
  mono.setColor(0, qRgb(100, 100, 100));
  mono.setColor(1, qRgb(100, 100, 100));
  mono.fill(0);
  mono.setPixel(0, 0, 1);
  mono.setPixel(2, 1, 1);

  const GrayscaleHistogram hist(mono);
  BOOST_CHECK_EQUAL(hist[100], 8);

  BinaryImage mask(4, 2, WHITE);
  mask.setPixel(0, 0, BLACK);
  mask.setPixel(1, 0, BLACK);
  mask.setPixel(2, 1, BLACK);
  const GrayscaleHistogram maskedHist(mono, mask);
  BOOST_CHECK_EQUAL(maskedHist[100], 3);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc