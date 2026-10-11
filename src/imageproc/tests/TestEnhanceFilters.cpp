// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <KnnDenoiser.h>
#include <Sharpen.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdlib>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(EnhanceFiltersTestSuite)

namespace {
// Left half dark (50), right half bright (200), optionally with deterministic noise of +-20.
GrayImage makeEdge(const bool noise) {
  GrayImage image(QSize(120, 60));
  unsigned seed = 12345;
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      int val = (x < 60) ? 50 : 200;
      if (noise) {
        seed = seed * 1103515245u + 12345u;
        val += static_cast<int>((seed >> 16) % 41) - 20;
      }
      line[x] = static_cast<uint8_t>(val);
    }
  }
  return image;
}

uint8_t at(const GrayImage& image, int x, int y) {
  return image.data()[y * image.stride() + x];
}

// Mean absolute deviation from the noise-free value inside the flat parts.
double noiseLevel(const GrayImage& image) {
  double sum = 0;
  int count = 0;
  for (int y = 10; y < 50; ++y) {
    for (int x = 10; x < 110; ++x) {
      if (std::abs(x - 60) < 10) {
        continue;
      }
      sum += std::abs(at(image, x, y) - ((x < 60) ? 50 : 200));
      ++count;
    }
  }
  return sum / count;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_off_and_null) {
  GrayImage null;
  knnDenoiseInPlace(null, 7, 0.5);
  sharpenInPlace(null, 2, 1.0);
  BOOST_CHECK(null.isNull());

  const GrayImage original = makeEdge(true);
  GrayImage image(original);
  knnDenoiseInPlace(image, 7, 0.0);
  sharpenInPlace(image, 2, 0.0);
  BOOST_CHECK(image == original);
}

BOOST_AUTO_TEST_CASE(test_knn_reduces_noise_and_keeps_the_edge) {
  GrayImage image = makeEdge(true);
  const double before = noiseLevel(image);
  knnDenoiseInPlace(image, 7, 0.5);
  BOOST_CHECK_LT(noiseLevel(image), 0.6 * before);
  // Next to the edge, both sides keep their level.
  for (int y = 10; y < 50; y += 5) {
    BOOST_CHECK_LT(at(image, 57, y), 90);
    BOOST_CHECK_GT(at(image, 62, y), 160);
  }
}

BOOST_AUTO_TEST_CASE(test_sharpen_steepens_the_edge) {
  const GrayImage original = makeEdge(false);
  GrayImage image(original);
  sharpenInPlace(image, 2, 1.0);
  // Overshoot on both sides of the edge, flat parts unchanged.
  BOOST_CHECK_LT(at(image, 59, 30), at(original, 59, 30));
  BOOST_CHECK_GT(at(image, 60, 30), at(original, 60, 30));
  BOOST_CHECK_EQUAL(at(image, 20, 30), 50);
  BOOST_CHECK_EQUAL(at(image, 100, 30), 200);
}

BOOST_AUTO_TEST_CASE(test_color_keeps_hue) {
  QImage image(60, 40, QImage::Format_RGB32);
  image.fill(qRgb(200, 60, 60));
  for (int y = 0; y < 40; ++y) {
    for (int x = 30; x < 60; ++x) {
      image.setPixel(x, y, qRgb(240, 240, 240));
    }
  }
  sharpenColorInPlace(image, 2, 1.0);
  knnDenoiseColorInPlace(image, 5, 0.5);
  const QRgb red = image.pixel(10, 20);
  BOOST_CHECK_GT(qRed(red), qGreen(red) + 100);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
