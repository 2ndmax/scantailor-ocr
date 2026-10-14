// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ColoredDimming.h>

#include <QImage>
#include <boost/test/unit_test.hpp>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(ColoredDimmingTestSuite)

namespace {
QImage onePixel(const QRgb color) {
  QImage image(1, 1, QImage::Format_RGB32);
  image.fill(color);
  return image;
}

QRgb dimmed(const QRgb color, const double coef) {
  return dimColoredPixels(onePixel(color), coef).pixel(0, 0);
}
}  // namespace

BOOST_AUTO_TEST_CASE(zero_changes_nothing) {
  const QImage image = onePixel(qRgb(200, 30, 40));
  BOOST_CHECK(dimColoredPixels(image, 0.0) == image);
}

BOOST_AUTO_TEST_CASE(gray_pixels_stay_as_they_are) {
  for (const double coef : {-1.0, 1.0, 2.0}) {
    BOOST_CHECK_EQUAL(dimmed(qRgb(90, 90, 90), coef), qRgb(90, 90, 90));
    BOOST_CHECK_EQUAL(dimmed(qRgb(255, 255, 255), coef), qRgb(255, 255, 255));
  }
}

BOOST_AUTO_TEST_CASE(positive_values_darken_saturated_colors) {
  // Saturation 200: 1.0 moves the pixel 200/255 of the way to black, 2.0 all the way.
  BOOST_CHECK_EQUAL(dimmed(qRgb(220, 20, 20), 1.0), qRgb(47, 4, 4));
  BOOST_CHECK_EQUAL(dimmed(qRgb(220, 20, 20), 2.0), qRgb(0, 0, 0));
}

BOOST_AUTO_TEST_CASE(negative_values_lighten_saturated_colors) {
  // Saturation 200: -1.0 moves the pixel 200/255 of the way to white.
  BOOST_CHECK_EQUAL(dimmed(qRgb(220, 20, 20), -1.0), qRgb(247, 204, 204));
}

BOOST_AUTO_TEST_CASE(weak_colors_change_less_than_strong_ones) {
  const QRgb strong = qRgb(220, 20, 20);
  const QRgb weak = qRgb(140, 100, 100);
  const int strongChange = qGray(strong) - qGray(dimmed(strong, 1.0));
  const int weakChange = qGray(weak) - qGray(dimmed(weak, 1.0));
  BOOST_CHECK_GT(weakChange, 0);
  BOOST_CHECK_LT(weakChange, strongChange / 2);
}

BOOST_AUTO_TEST_CASE(grayscale_images_are_returned_as_they_are) {
  QImage image(2, 2, QImage::Format_Grayscale8);
  image.fill(100);
  BOOST_CHECK(dimColoredPixels(image, 2.0) == image);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
