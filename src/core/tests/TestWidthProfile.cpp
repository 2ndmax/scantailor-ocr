// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <WidthProfile.h>

#include <boost/test/unit_test.hpp>
#include <cmath>

namespace Tests {
using namespace dewarping;
using imageproc::GrayImage;

BOOST_AUTO_TEST_SUITE(WidthProfileTestSuite)

namespace {
constexpr int kDpi = 600;
constexpr int kWidth = 2000;
constexpr int kHeight = 1200;

// Horizontal scale of the "printed" text at x: 1 on the left, falling to 0.75 at the right edge.
double compression(const double x) {
  return (x < 1400) ? 1.0 : 1.0 - 0.25 * (x - 1400) / (kWidth - 1400);
}

void fillRect(GrayImage& image, int x0, int y0, int w, int h) {
  for (int y = y0; y < y0 + h; ++y) {
    for (int x = x0; x < x0 + w; ++x) {
      image.data()[y * image.stride() + x] = 0;
    }
  }
}

// Rows of "H" shaped letters: stems 8 px wide, a bar 6 px thick.  With compress, letters and
// stems get narrower towards the right edge, as near the spine of a book.
GrayImage makePage(const bool compress) {
  GrayImage image(QSize(kWidth, kHeight));
  image.fill(255);
  for (int y = 40; y + 60 < kHeight - 40; y += 100) {
    double x = 40;
    while (x < kWidth - 80) {
      const double c = compress ? compression(x) : 1.0;
      const int stem = static_cast<int>(std::lround(8 * c));
      const int width = static_cast<int>(std::lround(40 * c));
      fillRect(image, static_cast<int>(x), y, stem, 60);
      fillRect(image, static_cast<int>(x) + width - stem, y, stem, 60);
      fillRect(image, static_cast<int>(x), y + 27, width, 6);
      x += 60 * c;
    }
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_empty_page_gives_nothing) {
  GrayImage image(QSize(800, 600));
  image.fill(255);
  BOOST_CHECK(WidthProfile::measure(image, QRect(0, 0, 800, 600), kDpi).empty());
}

BOOST_AUTO_TEST_CASE(test_even_page_needs_no_correction) {
  const GrayImage image = makePage(false);
  const auto samples = WidthProfile::measure(image, QRect(0, 0, kWidth, kHeight), kDpi);
  BOOST_REQUIRE_GE(samples.size(), 5u);
  for (const auto& sample : samples) {
    BOOST_CHECK_SMALL(sample.scale - 1.0, 0.03);
  }
  BOOST_CHECK(WidthProfile::toCorrection(samples, 0, kWidth).isNull());
}

BOOST_AUTO_TEST_CASE(test_compressed_edge_is_stretched) {
  const GrayImage image = makePage(true);
  const auto samples = WidthProfile::measure(image, QRect(0, 0, kWidth, kHeight), kDpi);
  BOOST_REQUIRE_GE(samples.size(), 5u);
  // The measured scale follows the compression of the stems.
  BOOST_CHECK_SMALL(samples.front().scale - 1.0, 0.05);
  BOOST_CHECK_LT(samples.back().scale, 0.88);

  const WidthCorrection correction = WidthProfile::toCorrection(samples, 0, kWidth);
  BOOST_REQUIRE(!correction.isNull());
  // Stretched near the right edge, slightly compressed on the left to keep the width.
  const double slopeRight = (correction.map(1.0) - correction.map(0.95)) / 0.05;
  const double slopeLeft = (correction.map(0.3) - correction.map(0.1)) / 0.2;
  BOOST_CHECK_GT(slopeRight, 1.1);
  BOOST_CHECK_LT(slopeLeft, 1.0);
  BOOST_CHECK_GT(slopeLeft, 0.85);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
