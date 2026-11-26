// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BrightnessFilter.h>
#include <ChromaDenoiser.h>
#include <ClampToByte.h>
#include <GrayImage.h>
#include <KnnDenoiser.h>
#include <Sharpen.h>
#include <WienerFilter.h>

#include <QImage>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>

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

BOOST_AUTO_TEST_CASE(test_chroma_denoise_removes_color_noise) {
  // Gray paper (180) with single red, green and blue pixels of about the same brightness,
  // and a strongly colored patch.
  QImage image(80, 60, QImage::Format_RGB32);
  image.fill(qRgb(180, 180, 180));
  const QRgb noise[] = {qRgb(215, 165, 175), qRgb(165, 200, 160), qRgb(170, 175, 215)};
  for (int i = 0; i < 300; ++i) {
    image.setPixel((i * 37) % 40, (i * 53) % 60, noise[i % 3]);
  }
  for (int y = 20; y < 40; ++y) {
    for (int x = 55; x < 75; ++x) {
      image.setPixel(x, y, qRgb(200, 30, 30));
    }
  }
  const QImage original = image.copy();
  chromaDenoiseInPlace(image, 3, 15);

  int maxSpread = 0;
  for (int y = 0; y < 60; ++y) {
    for (int x = 0; x < 40; ++x) {
      const QRgb px = image.pixel(x, y);
      maxSpread = std::max(maxSpread,
                           std::max({qRed(px), qGreen(px), qBlue(px)}) - std::min({qRed(px), qGreen(px), qBlue(px)}));
      // The brightness (as in JPEG) stays.
      const QRgb o = original.pixel(x, y);
      const auto luma = [](QRgb c) { return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c); };
      BOOST_REQUIRE_SMALL(luma(px) - luma(o), 1.5);
    }
  }
  BOOST_CHECK_LE(maxSpread, 3);
  const QRgb red = image.pixel(65, 30);
  BOOST_CHECK_GT(qRed(red), qGreen(red) + 120);
}

BOOST_AUTO_TEST_CASE(test_wiener_keeps_alpha) {
  const GrayImage gray(makeEdge(true));
  QImage image(gray.width(), gray.height(), QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const int val = at(gray, x, y);
      image.setPixel(x, y, qRgba(val, val, val, 255));
    }
  }
  const QImage original(image);

  wienerColorFilterInPlace(image, QSize(5, 5), 0.5);

  BOOST_CHECK(image != original);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      BOOST_REQUIRE_EQUAL(qAlpha(image.pixel(x, y)), 255);
    }
  }
}

// std::clamp() lets NaN through, and the cast of NaN to a byte is undefined.
BOOST_AUTO_TEST_CASE(test_clamp_to_byte_maps_nan_to_0) {
  BOOST_CHECK_EQUAL(int(clampToByte(std::nanf(""))), 0);
  BOOST_CHECK_EQUAL(int(clampToByte(-3.0f)), 0);
  BOOST_CHECK_EQUAL(int(clampToByte(254.6f)), 255);
}

BOOST_AUTO_TEST_CASE(test_wiener_handles_indexed_color_and_rgb888) {
  QImage indexed(60, 40, QImage::Format_Indexed8);
  indexed.setColorTable({qRgb(200, 60, 60), qRgb(240, 240, 240)});
  for (int y = 0; y < indexed.height(); ++y) {
    for (int x = 0; x < indexed.width(); ++x) {
      indexed.setPixel(x, y, (x < 30) ? 0 : 1);
    }
  }
  wienerColorFilterInPlace(indexed, QSize(5, 5), 0.5);
  const QRgb indexedPx = indexed.pixel(10, 20);
  BOOST_CHECK_GT(qRed(indexedPx), qGreen(indexedPx) + 100);

  QImage rgb888(60, 40, QImage::Format_RGB888);
  unsigned seed = 54321;
  for (int y = 0; y < rgb888.height(); ++y) {
    for (int x = 0; x < rgb888.width(); ++x) {
      seed = seed * 1103515245u + 12345u;
      const int delta = static_cast<int>((seed >> 16) % 21) - 10;
      if (x < 30) {
        rgb888.setPixel(x, y, qRgb(200 + delta, 60, 60));
      } else {
        rgb888.setPixel(x, y, qRgb(230 + delta, 230 + delta, 230 + delta));
      }
    }
  }
  const QImage originalRgb888 = rgb888.copy();
  wienerColorFilterInPlace(rgb888, QSize(5, 5), 0.5);
  BOOST_CHECK(rgb888 != originalRgb888);
  const QRgb rgbPx = rgb888.pixel(10, 20);
  BOOST_CHECK_GT(qRed(rgbPx), qGreen(rgbPx) + 100);
}

BOOST_AUTO_TEST_CASE(test_brightness_filter_preserves_indexed8_color_hue) {
  QImage indexed(40, 20, QImage::Format_Indexed8);
  indexed.setColorTable({qRgb(200, 40, 40), qRgb(40, 180, 40), qRgb(240, 240, 240)});
  for (int y = 0; y < indexed.height(); ++y) {
    for (int x = 0; x < indexed.width(); ++x) {
      indexed.setPixel(x, y, (x < 15) ? 0 : ((x < 30) ? 1 : 2));
    }
  }

  filterBrightnessInPlace(indexed, [](GrayImage& gray) {
    for (int y = 0; y < gray.height(); ++y) {
      uint8_t* line = gray.data() + y * gray.stride();
      for (int x = 0; x < gray.width(); ++x) {
        line[x] = clampToByte(static_cast<int>(line[x]) + 20);
      }
    }
  });

  const QRgb redPx = indexed.pixel(5, 10);
  const QRgb greenPx = indexed.pixel(20, 10);
  BOOST_CHECK_GT(qRed(redPx), qGreen(redPx) + 80);
  BOOST_CHECK_GT(qGreen(greenPx), qRed(greenPx) + 80);
}

BOOST_AUTO_TEST_CASE(test_wiener_and_knn_guard_nan_parameters) {
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  GrayImage gray = makeEdge(true);
  const GrayImage grayOrig(gray);
  for (const double bad : {qnan, inf, -1.0}) {
    BOOST_CHECK_THROW(wienerFilterInPlace(gray, QSize(5, 5), bad), std::invalid_argument);
  }
  BOOST_CHECK(gray == grayOrig);

  for (const double bad : {qnan, inf, 0.0, -0.5}) {
    knnDenoiseInPlace(gray, 5, bad);
    BOOST_CHECK(gray == grayOrig);
  }

  QImage color(24, 24, QImage::Format_RGB32);
  color.fill(qRgb(180, 140, 120));
  const QImage colorOrig = color.copy();
  for (const double bad : {qnan, inf, 0.0}) {
    wienerColorFilterInPlace(color, QSize(5, 5), bad);
    knnDenoiseColorInPlace(color, 5, bad);
    BOOST_CHECK(color == colorOrig);
  }
}

BOOST_AUTO_TEST_CASE(test_sharpen_guards_nan_and_invalid_radius) {
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  GrayImage gray = makeEdge(false);
  const GrayImage grayOrig(gray);
  for (const double bad : {qnan, inf, 0.0, -1.0}) {
    sharpenInPlace(gray, 2, bad);
    BOOST_CHECK(gray == grayOrig);
  }
  sharpenInPlace(gray, 0, 1.0);
  BOOST_CHECK(gray == grayOrig);

  QImage color(24, 24, QImage::Format_RGB32);
  color.fill(qRgb(180, 140, 120));
  const QImage colorOrig = color.copy();
  for (const double bad : {qnan, inf, 0.0, -1.0}) {
    sharpenColorInPlace(color, 2, bad);
    BOOST_CHECK(color == colorOrig);
  }
  sharpenColorInPlace(color, -2, 1.0);
  BOOST_CHECK(color == colorOrig);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
