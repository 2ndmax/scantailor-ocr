// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Posterizer.h>

#include <QImage>
#include <boost/test/unit_test.hpp>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(PosterizerTestSuite)

namespace {
// An image with the given number of different colors, one per pixel in a row.
QImage imageWithColors(const int colors) {
  QImage image(colors, 1, QImage::Format_RGB32);
  for (int x = 0; x < colors; ++x) {
    image.setPixel(x, 0, qRgb(x, 255 - x, 100));
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_CASE(the_palette_has_one_entry_per_color) {
  BOOST_CHECK_EQUAL(Posterizer::buildPalette(imageWithColors(3)).size(), 3);
}

BOOST_AUTO_TEST_CASE(up_to_256_colors_give_an_indexed_image) {
  const QImage image = imageWithColors(200);
  const QImage indexed = Posterizer::convertToIndexed(image);
  BOOST_REQUIRE(indexed.format() == QImage::Format_Indexed8);
  BOOST_CHECK_EQUAL(indexed.colorCount(), 200);
  bool same = true;
  for (int x = 0; x < image.width(); ++x) {
    same = same && (indexed.pixel(x, 0) == image.pixel(x, 0));
  }
  BOOST_CHECK(same);
}

BOOST_AUTO_TEST_CASE(normalizing_a_page_of_one_tone_keeps_it) {
  QImage image(20, 20, QImage::Format_RGB32);
  image.fill(qRgb(200, 200, 200));
  const QImage result = Posterizer(4, true).posterize(image);
  BOOST_CHECK_EQUAL(result.pixel(5, 5), qRgb(200, 200, 200));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
