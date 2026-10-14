// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ColorKMeans.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <set>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(ColorKMeansTestSuite)

namespace {
// White background with stripes of slightly different reds, blues and near-blacks, as the letter
// colors of a page after color segmentation.
QImage letterColors() {
  QImage image(60, 30, QImage::Format_RGB32);
  image.fill(Qt::white);
  const QRgb colors[] = {qRgb(200, 30, 30), qRgb(190, 40, 35), qRgb(210, 25, 40),  // reds
                         qRgb(30, 40, 180), qRgb(35, 50, 170),                     // blues
                         qRgb(20, 20, 25),  qRgb(35, 30, 30),  qRgb(10, 15, 10)};  // near-blacks
  int x = 0;
  for (const QRgb color : colors) {
    for (int y = 0; y < 20; ++y) {
      image.setPixel(x, y, color);
      image.setPixel(x + 1, y, color);
    }
    x += 4;
  }
  return image;
}

std::set<QRgb> colorsOf(const QImage& image) {
  std::set<QRgb> colors;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      colors.insert(image.pixel(x, y));
    }
  }
  return colors;
}
}  // namespace

BOOST_AUTO_TEST_CASE(colors_are_reduced_to_the_given_number_plus_white) {
  const std::set<QRgb> colors = colorsOf(reduceColorsKMeans(letterColors(), 3, true));
  BOOST_CHECK_EQUAL(colors.size(), 4u);
  BOOST_CHECK(colors.count(qRgb(255, 255, 255)) == 1);
}

BOOST_AUTO_TEST_CASE(fixed_black_stays_pure_black) {
  const QImage result = reduceColorsKMeans(letterColors(), 3, true);
  // The near-black stripes become pure black.
  BOOST_CHECK_EQUAL(result.pixel(20, 5), qRgb(0, 0, 0));
  BOOST_CHECK_EQUAL(result.pixel(28, 5), qRgb(0, 0, 0));
}

BOOST_AUTO_TEST_CASE(similar_colors_end_up_in_one_group) {
  const QImage result = reduceColorsKMeans(letterColors(), 3, true);
  // All reds get the same color, a red one; all blues the same blue.
  BOOST_CHECK_EQUAL(result.pixel(0, 5), result.pixel(4, 5));
  BOOST_CHECK_EQUAL(result.pixel(0, 5), result.pixel(8, 5));
  BOOST_CHECK_GT(qRed(result.pixel(0, 5)), 150);
  BOOST_CHECK_EQUAL(result.pixel(12, 5), result.pixel(16, 5));
  BOOST_CHECK_GT(qBlue(result.pixel(12, 5)), 150);
}

BOOST_AUTO_TEST_CASE(the_background_stays_white) {
  const QImage result = reduceColorsKMeans(letterColors(), 3, false);
  BOOST_CHECK_EQUAL(result.pixel(59, 29), qRgb(255, 255, 255));
}

BOOST_AUTO_TEST_CASE(the_result_is_always_the_same) {
  BOOST_CHECK(reduceColorsKMeans(letterColors(), 4, false) == reduceColorsKMeans(letterColors(), 4, false));
}

BOOST_AUTO_TEST_CASE(fewer_colors_than_groups_are_kept) {
  QImage image(4, 1, QImage::Format_RGB32);
  image.fill(Qt::white);
  image.setPixel(0, 0, qRgb(200, 0, 0));
  image.setPixel(1, 0, qRgb(0, 0, 200));
  const QImage result = reduceColorsKMeans(image, 6, false);
  BOOST_CHECK_EQUAL(result.pixel(0, 0), qRgb(200, 0, 0));
  BOOST_CHECK_EQUAL(result.pixel(1, 0), qRgb(0, 0, 200));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
