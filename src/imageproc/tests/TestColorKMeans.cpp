// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <ColorKMeans.h>
#include <ConnectivityMap.h>

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

namespace {
// A small red "letter" with an almost black rim, and a large area that is dark gray on the left and
// middle gray on the right, as a picture; the text mask covers both.
struct TextPage {
  QImage color;
  BinaryImage text;
};

TextPage textPage() {
  TextPage page{QImage(200, 120, QImage::Format_RGB32), BinaryImage(200, 120, WHITE)};
  page.color.fill(Qt::white);
  for (int y = 10; y < 20; ++y) {
    for (int x = 10; x < 20; ++x) {
      const bool rim = (x == 10) || (x == 19) || (y == 10) || (y == 19);
      page.color.setPixel(x, y, rim ? qRgb(30, 10, 10) : qRgb(220, 30, 30));
      page.text.setPixel(x, y, BLACK);
    }
  }
  for (int y = 10; y < 110; ++y) {
    for (int x = 60; x < 190; ++x) {
      page.color.setPixel(x, y, (x < 125) ? qRgb(40, 40, 40) : qRgb(130, 130, 130));
      page.text.setPixel(x, y, BLACK);
    }
  }
  return page;
}
}  // namespace

BOOST_AUTO_TEST_CASE(letters_get_one_color_and_large_areas_keep_their_shading) {
  const TextPage page = textPage();
  const QImage result = reduceTextColorsKMeans(page.color, page.text, 4, true, 400);
  // The letter (100 pixels) in one red color, rim included.
  BOOST_CHECK_EQUAL(result.pixel(10, 10), result.pixel(15, 15));
  BOOST_CHECK_GT(qRed(result.pixel(15, 15)), 120);
  // The large area (13000 pixels) keeps its two tones.
  BOOST_CHECK_NE(result.pixel(70, 50), result.pixel(180, 50));
  // Outside the text: white.
  BOOST_CHECK_EQUAL(result.pixel(5, 5), qRgb(255, 255, 255));
}

BOOST_AUTO_TEST_CASE(pixel_by_pixel_keeps_the_rim_of_letters) {
  const TextPage page = textPage();
  // Pixel by pixel, the rim becomes black and only the inside red.
  const QImage result = reduceTextColorsKMeans(page.color, page.text, 4, true, 0);
  BOOST_CHECK_EQUAL(result.pixel(10, 10), qRgb(0, 0, 0));
  BOOST_CHECK_GT(qRed(result.pixel(15, 15)), 120);
}

BOOST_AUTO_TEST_CASE(the_connected_areas_as_segments_give_the_same_result) {
  const TextPage page = textPage();
  BOOST_CHECK(reduceTextColorsKMeans(page.color, page.text, ConnectivityMap(page.text, CONN8), 4, true, 400)
              == reduceTextColorsKMeans(page.color, page.text, 4, true, 400));
}

BOOST_AUTO_TEST_CASE(touching_letters_of_two_segments_get_two_colors) {
  // Two touching "letters", red on the left, black on the right: one connected area, two segments.
  QImage color(40, 20, QImage::Format_RGB32);
  color.fill(Qt::white);
  BinaryImage text(40, 20, WHITE);
  BinaryImage left(40, 20, WHITE);
  BinaryImage right(40, 20, WHITE);
  for (int y = 5; y < 15; ++y) {
    for (int x = 5; x < 25; ++x) {
      color.setPixel(x, y, (x < 15) ? qRgb(220, 30, 30) : qRgb(15, 15, 15));
      text.setPixel(x, y, BLACK);
      ((x < 15) ? left : right).setPixel(x, y, BLACK);
    }
  }
  ConnectivityMap segments(left, CONN8);
  segments.addComponents(right, CONN8);

  // Two colors: the fixed black and a red.
  const QImage result = reduceTextColorsKMeans(color, text, segments, 2, true, 400);
  BOOST_CHECK_GT(qRed(result.pixel(8, 10)), 120);
  BOOST_CHECK_EQUAL(result.pixel(20, 10), qRgb(0, 0, 0));
  // Without the segments, both get the one color of their average.
  const QImage whole = reduceTextColorsKMeans(color, text, 2, true, 400);
  BOOST_CHECK_EQUAL(whole.pixel(8, 10), whole.pixel(20, 10));
}

BOOST_AUTO_TEST_CASE(large_areas_keep_their_shading_whatever_the_segments) {
  const TextPage page = textPage();
  // The large area is one segment here, and still keeps its two tones.
  const QImage result = reduceTextColorsKMeans(page.color, page.text, ConnectivityMap(page.text, CONN8), 4, true, 400);
  BOOST_CHECK_NE(result.pixel(70, 50), result.pixel(180, 50));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
