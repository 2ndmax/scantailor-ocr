// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GaussBlur.h>
#include <GrayImage.h>

#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cstdlib>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(GaussBlurTestSuite)

namespace {
// The largest difference of a pixel to 'value'.
int maxDeviation(const GrayImage& image, const int value) {
  int deviation = 0;
  for (int y = 0; y < image.height(); ++y) {
    const uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      deviation = std::max(deviation, std::abs(line[x] - value));
    }
  }
  return deviation;
}
}  // namespace

// Blurring an even area leaves it as it is, whatever the sigma. In single precision the filter
// went wrong from a sigma of about 30 (Robust binarization uses half the window size, 100 by
// default), and a huge sigma from a project file gave NaN.
BOOST_AUTO_TEST_CASE(even_area_stays_even) {
  GrayImage image(QSize(1500, 1500));
  image.fill(200);
  for (const float sigma : {1.0f, 30.0f, 100.0f, 1000.0f, 5000.0f, 1e9f}) {
    BOOST_CHECK_MESSAGE(maxDeviation(gaussBlur(image, sigma, sigma), 200) <= 1, "sigma " << sigma);
  }
}

// A sigma of 0 or below means no blur; it divided by 0 before.
BOOST_AUTO_TEST_CASE(sigma_zero_or_below_leaves_the_image) {
  GrayImage image(QSize(50, 40));
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.data()[y * image.stride() + x] = static_cast<uint8_t>((x * 7 + y * 13) % 256);
    }
  }
  for (const float sigma : {0.0f, -3.0f}) {
    const GrayImage blurred = gaussBlur(image, sigma, sigma);
    BOOST_CHECK_MESSAGE(blurred == image, "sigma " << sigma);
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
