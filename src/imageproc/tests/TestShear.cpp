// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BWColor.h>
#include <BinaryImage.h>
#include <Shear.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <stdexcept>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(ShearTestSuite)

BOOST_AUTO_TEST_CASE(test_small_image) {
  static const int inp[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0,
                            0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0,
                            0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  static const int h_out[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1,
                              0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0,
                              1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  static const int v_out[] = {0, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0,
                              0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0,
                              0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0};

  const BinaryImage img(makeBinaryImage(inp, 9, 9));
  const BinaryImage hOutImg(makeBinaryImage(h_out, 9, 9));
  const BinaryImage vOutImg(makeBinaryImage(v_out, 9, 9));

  const BinaryImage hShear = imageproc::hShear(img, -1.0, 0.5 * img.height(), WHITE);
  BOOST_REQUIRE(hShear == hOutImg);

  const BinaryImage vShear = imageproc::vShear(img, 1.0, 0.5 * img.width(), WHITE);
  BOOST_REQUIRE(vShear == vOutImg);

  BinaryImage hShearInplace(img);
  hShearInPlace(hShearInplace, -1.0, 0.5 * img.height(), WHITE);
  BOOST_REQUIRE(hShearInplace == hOutImg);

  BinaryImage vShearInplace(img);
  vShearInPlace(vShearInplace, 1.0, 0.5 * img.width(), WHITE);
  BOOST_REQUIRE(vShearInplace == vOutImg);
}

BOOST_AUTO_TEST_CASE(test_constant_nonzero_shift_and_nonfinite_parameters) {
  BinaryImage hImg(10, 1, WHITE);
  hImg.setPixel(2, 0, BLACK);
  const BinaryImage hOut = imageproc::hShear(hImg, 1.0, -2.5, WHITE);
  BOOST_CHECK_EQUAL(hOut.getPixel(5, 0), BLACK);
  BOOST_CHECK_EQUAL(hOut.getPixel(2, 0), WHITE);

  BinaryImage vImg(1, 10, WHITE);
  vImg.setPixel(0, 2, BLACK);
  const BinaryImage vOut = imageproc::vShear(vImg, 1.0, -2.5, WHITE);
  BOOST_CHECK_EQUAL(vOut.getPixel(0, 5), BLACK);
  BOOST_CHECK_EQUAL(vOut.getPixel(0, 2), WHITE);

  BOOST_CHECK_THROW(imageproc::hShear(hImg, std::numeric_limits<double>::quiet_NaN(), 0.0, WHITE),
                    std::invalid_argument);
  BOOST_CHECK_THROW(imageproc::vShear(vImg, 0.0, std::numeric_limits<double>::infinity(), WHITE),
                    std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc