// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <ImageCombination.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <stdexcept>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(ImageCombinationTestSuite)

BOOST_AUTO_TEST_CASE(test_gray_foreground_white_is_transparent) {
  static const int mixedData[] = {100, 100, 100, 100};
  static const int foregroundData[] = {0, 50, 255, 255};
  QImage mixed(makeGrayImage(mixedData, 4, 1));

  combineImages(mixed, makeGrayImage(foregroundData, 4, 1));

  BOOST_CHECK_EQUAL(qGray(mixed.pixel(0, 0)), 0);
  BOOST_CHECK_EQUAL(qGray(mixed.pixel(1, 0)), 50);
  BOOST_CHECK_EQUAL(qGray(mixed.pixel(2, 0)), 100);
  BOOST_CHECK_EQUAL(qGray(mixed.pixel(3, 0)), 100);
}

BOOST_AUTO_TEST_CASE(test_gray_foreground_with_own_palette) {
  // A posterized foreground has a short gray palette of its own, so its indices are not grays.
  static const int mixedData[] = {100, 100, 100};
  QImage mixed(makeGrayImage(mixedData, 3, 1));

  QImage foreground(3, 1, QImage::Format_Indexed8);
  foreground.setColorTable({qRgb(0, 0, 0), qRgb(128, 128, 128), qRgb(255, 255, 255)});
  for (int x = 0; x < 3; ++x) {
    foreground.setPixel(x, 0, x);
  }

  combineImages(mixed, foreground);

  BOOST_CHECK_EQUAL(qGray(mixed.pixel(0, 0)), 0);
  BOOST_CHECK_EQUAL(qGray(mixed.pixel(1, 0)), 128);
  BOOST_CHECK_EQUAL(qGray(mixed.pixel(2, 0)), 100);
}

BOOST_AUTO_TEST_CASE(test_mask_size_is_checked) {
  static const int mixedData[] = {100, 100, 100, 100};
  QImage mixed(makeGrayImage(mixedData, 4, 1));
  const BinaryImage foreground(4, 1, BLACK);
  const BinaryImage mask(2, 1, BLACK);

  BOOST_CHECK_THROW(combineImages(mixed, foreground, mask), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
