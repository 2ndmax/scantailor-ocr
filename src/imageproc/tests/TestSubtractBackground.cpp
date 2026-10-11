// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <SubtractBackground.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <cstdlib>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(SubtractBackgroundTestSuite)

namespace {
// A background getting brighter from left to right, with small dark "letters", away from the edges.
GrayImage makePage(const int w, const int h) {
  GrayImage image(QSize(w, h));
  for (int y = 0; y < h; ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < w; ++x) {
      const bool letter = (x % 20 >= 10) && (x % 20 < 13) && (y % 20 >= 10) && (y % 20 < 18);
      line[x] = letter ? 20 : static_cast<uint8_t>(150 + 80 * x / (w - 1));
    }
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_null_image) {
  GrayImage gray;
  subtractBackgroundInPlace(gray, 10, 1.0);
  BOOST_CHECK(gray.isNull());

  QImage image;
  subtractBackgroundColorInPlace(image, 10, 1.0);
  BOOST_CHECK(image.isNull());
}

BOOST_AUTO_TEST_CASE(test_off_leaves_image_unchanged) {
  const GrayImage original = makePage(200, 100);
  GrayImage image(original);
  subtractBackgroundInPlace(image, 15, 0.0);
  BOOST_CHECK(image == original);
}

BOOST_AUTO_TEST_CASE(test_uniform_image_unchanged) {
  GrayImage image(QSize(120, 80));
  image.fill(200);
  const GrayImage original(image);
  subtractBackgroundInPlace(image, 30, 1.0);
  BOOST_CHECK(image == original);
}

BOOST_AUTO_TEST_CASE(test_background_evened_out) {
  // Both the direct and the reduced median path.
  for (const int radius : {6, 40}) {
    GrayImage image = makePage(400, 200);
    subtractBackgroundInPlace(image, radius, 1.0);
    for (int y = 0; y < image.height(); ++y) {
      const uint8_t* line = image.data() + y * image.stride();
      for (int x = 0; x < image.width(); ++x) {
        const bool letter = (x % 20 >= 10) && (x % 20 < 13) && (y % 20 >= 10) && (y % 20 < 18);
        if (letter) {
          BOOST_REQUIRE_LT(line[x], 120);
        } else {
          BOOST_REQUIRE_GE(line[x], 220);
        }
      }
    }
  }
}

BOOST_AUTO_TEST_CASE(test_color_keeps_hue) {
  QImage image(100, 60, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const int bg = 150 + x;
      image.setPixel(x, y, qRgb(bg, bg, bg - 20));
    }
  }
  image.setPixel(50, 30, qRgb(200, 30, 30));
  subtractBackgroundColorInPlace(image, 10, 1.0);

  const QRgb red = image.pixel(50, 30);
  BOOST_CHECK_GT(qRed(red), qGreen(red) + 100);
  // The paper on the left has been brightened to the level of the right side.
  BOOST_CHECK_GT(qRed(image.pixel(5, 30)), 230);
}

BOOST_AUTO_TEST_CASE(test_white_paper_removes_yellowing) {
  QImage image(100, 60, QImage::Format_RGB32);
  image.fill(qRgb(235, 220, 160));
  for (int x = 40; x < 44; ++x) {
    for (int y = 25; y < 35; ++y) {
      image.setPixel(x, y, qRgb(40, 35, 25));
    }
  }
  subtractBackgroundColorInPlace(image, 10, 1.0, true);

  const QRgb paper = image.pixel(10, 10);
  BOOST_CHECK_GE(qRed(paper), 253);
  BOOST_CHECK_GE(qGreen(paper), 253);
  BOOST_CHECK_GE(qBlue(paper), 253);
  const QRgb text = image.pixel(41, 30);
  BOOST_CHECK_LT(qRed(text) + qGreen(text) + qBlue(text), 3 * 80);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
