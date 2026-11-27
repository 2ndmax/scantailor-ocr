// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AdjustBrightness.h>
#include <BWColor.h>
#include <BackgroundColorCalculator.h>
#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <ColorInterpolation.h>
#include <ColorSegmenter.h>
#include <Connectivity.h>
#include <ConnectivityMap.h>
#include <Dpi.h>
#include <DrawOver.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <InfluenceMap.h>
#include <MorphGradientDetect.h>
#include <UpscaleIntegerTimes.h>

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(ImageprocModulesTestSuite)

BOOST_AUTO_TEST_CASE(test_background_color_calculator) {
  QImage gray(30, 30, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());
  gray.fill(220);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      gray.setPixel(x, y, 20);
    }
  }
  const QColor grayBg = BackgroundColorCalculator::calcDominantBackgroundColor(gray);
  BOOST_CHECK_CLOSE(static_cast<double>(grayBg.red()), 220.0, 2.0);

  QImage rgb(30, 30, QImage::Format_RGB32);
  rgb.fill(qRgb(240, 230, 220));
  BinaryImage mask(30, 30, BLACK);
  mask.fill(QRect(0, 0, 5, 5), WHITE);
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 5; ++x) {
      rgb.setPixel(x, y, qRgb(10, 10, 10));
    }
  }
  const QColor rgbBg = BackgroundColorCalculator::calcDominantBackgroundColor(rgb, mask);
  BOOST_CHECK_GT(rgbBg.red(), 220);
  BOOST_CHECK_GT(rgbBg.green(), 210);
  BOOST_CHECK_GT(rgbBg.blue(), 200);
}

BOOST_AUTO_TEST_CASE(test_color_segmenter) {
  QImage color(40, 20, QImage::Format_RGB32);
  color.fill(Qt::white);
  BinaryImage mask(40, 20, WHITE);
  for (int y = 4; y < 14; ++y) {
    for (int x = 4; x < 12; ++x) {
      color.setPixel(x, y, qRgb(210, 30, 30));
      mask.setPixel(x, y, BLACK);
    }
    for (int x = 24; x < 32; ++x) {
      color.setPixel(x, y, qRgb(30, 30, 210));
      mask.setPixel(x, y, BLACK);
    }
  }

  const ColorSegmenter segmenter(Dpi(300, 300), 10);
  const QImage segmented = segmenter.segment(mask, color);
  BOOST_REQUIRE(segmented.size() == color.size());
  BOOST_CHECK_EQUAL(segmented.format(), QImage::Format_RGB32);
  BOOST_CHECK_GT(qRed(segmented.pixel(8, 8)), 150);
  BOOST_CHECK_GT(qBlue(segmented.pixel(28, 8)), 150);
  BOOST_CHECK_EQUAL(segmented.pixel(0, 0), qRgb(255, 255, 255));
}

BOOST_AUTO_TEST_CASE(test_influence_map) {
  BinaryImage seeds(21, 21, WHITE);
  seeds.setPixel(2, 10, BLACK);
  seeds.setPixel(18, 10, BLACK);

  const ConnectivityMap cmap(seeds, CONN8);
  BOOST_REQUIRE_EQUAL(cmap.maxLabel(), 2u);
  const uint32_t leftLabel = cmap.data()[10 * cmap.stride() + 2];
  const uint32_t rightLabel = cmap.data()[10 * cmap.stride() + 18];
  BOOST_REQUIRE_NE(leftLabel, rightLabel);

  const InfluenceMap imap(cmap);
  BOOST_CHECK_EQUAL(imap.data()[10 * imap.stride() + 5].label, leftLabel);
  BOOST_CHECK_EQUAL(imap.data()[10 * imap.stride() + 15].label, rightLabel);

  const ConnectivityMap extended(imap);
  BOOST_CHECK_EQUAL(extended.maxLabel(), cmap.maxLabel());
  BOOST_CHECK_EQUAL(extended.data()[10 * extended.stride() + 5], leftLabel);
}

BOOST_AUTO_TEST_CASE(test_morph_gradient_detect) {
  GrayImage flat(QSize(16, 16));
  flat.fill(120);
  const GrayImage flatGrad = morphGradientDetectDarkSide(flat, QSize(3, 3));
  for (int y = 0; y < flatGrad.height(); ++y) {
    for (int x = 0; x < flatGrad.width(); ++x) {
      BOOST_CHECK_EQUAL(flatGrad.data()[y * flatGrad.stride() + x], 0);
    }
  }

  GrayImage step(QSize(16, 16));
  for (int y = 0; y < 16; ++y) {
    uint8_t* line = step.data() + y * step.stride();
    for (int x = 0; x < 16; ++x) {
      line[x] = (x < 8) ? 40 : 200;
    }
  }
  const GrayImage darkSide = morphGradientDetectDarkSide(step, QSize(3, 3));
  const GrayImage lightSide = morphGradientDetectLightSide(step, QSize(3, 3));
  BOOST_CHECK_GT(darkSide.data()[8 * darkSide.stride() + 7], 100);
  BOOST_CHECK_EQUAL(darkSide.data()[8 * darkSide.stride() + 8], 0);
  BOOST_CHECK_GT(lightSide.data()[8 * lightSide.stride() + 8], 100);
  BOOST_CHECK_EQUAL(lightSide.data()[8 * lightSide.stride() + 7], 0);
}

BOOST_AUTO_TEST_CASE(test_upscale_integer_times) {
  BinaryImage src(4, 4, WHITE);
  src.setPixel(1, 2, BLACK);

  const BinaryImage scaled = upscaleIntegerTimes(src, 3, 2);
  BOOST_REQUIRE(scaled.size() == QSize(12, 8));
  BOOST_CHECK_EQUAL(scaled.countBlackPixels(), 6);
  BOOST_CHECK_EQUAL(scaled.getPixel(3, 4), BLACK);
  BOOST_CHECK_EQUAL(scaled.getPixel(5, 5), BLACK);
  BOOST_CHECK_EQUAL(scaled.getPixel(2, 4), WHITE);

  const BinaryImage padded = upscaleIntegerTimes(src, QSize(14, 10), WHITE);
  BOOST_REQUIRE(padded.size() == QSize(14, 10));
  BOOST_CHECK_EQUAL(padded.countBlackPixels(), 6);
}

BOOST_AUTO_TEST_CASE(test_draw_over) {
  QImage dst(10, 10, QImage::Format_RGB32);
  dst.fill(qRgb(10, 20, 30));
  QImage src(6, 6, QImage::Format_RGB32);
  src.fill(qRgb(200, 150, 100));

  drawOver(dst, QRect(2, 3, 4, 4), src, QRect(1, 1, 4, 4));
  BOOST_CHECK_EQUAL(dst.pixel(2, 3), qRgb(200, 150, 100));
  BOOST_CHECK_EQUAL(dst.pixel(5, 6), qRgb(200, 150, 100));
  BOOST_CHECK_EQUAL(dst.pixel(1, 3), qRgb(10, 20, 30));
  BOOST_CHECK_EQUAL(dst.pixel(6, 6), qRgb(10, 20, 30));
}

BOOST_AUTO_TEST_CASE(test_adjust_brightness_and_color_interpolation) {
  QImage rgb(4, 4, QImage::Format_RGB32);
  rgb.fill(qRgb(100, 120, 140));
  QImage target(4, 4, QImage::Format_Indexed8);
  target.setColorTable(createGrayscalePalette());
  target.fill(200);

  adjustBrightnessGrayscale(rgb, target);
  const QRgb brightened = rgb.pixel(0, 0);
  BOOST_CHECK_GT(qRed(brightened), 100);
  BOOST_CHECK_GT(qGreen(brightened), 120);
  BOOST_CHECK_GT(qBlue(brightened), 140);

  const QColor from(0, 100, 200);
  const QColor to(100, 200, 0);
  const QColor c0 = colorInterpolation(from, to, 0.0);
  const QColor c1 = colorInterpolation(from, to, 1.0);
  const QColor mid = colorInterpolation(from, to, 0.5);
  BOOST_CHECK_EQUAL(c0.red(), 0);
  BOOST_CHECK_EQUAL(c0.green(), 100);
  BOOST_CHECK_EQUAL(c0.blue(), 200);
  BOOST_CHECK_EQUAL(c1.red(), 100);
  BOOST_CHECK_EQUAL(c1.green(), 200);
  BOOST_CHECK_EQUAL(c1.blue(), 0);
  BOOST_CHECK_EQUAL(mid.red(), 50);
  BOOST_CHECK_EQUAL(mid.green(), 150);
  BOOST_CHECK_EQUAL(mid.blue(), 100);
}

BOOST_AUTO_TEST_CASE(test_binary_threshold_otsu_and_peak) {
  QImage gray(40, 20, QImage::Format_Indexed8);
  gray.setColorTable(createGrayscalePalette());
  for (int y = 0; y < 20; ++y) {
    for (int x = 0; x < 40; ++x) {
      gray.setPixel(x, y, (x < 20) ? 30 : 220);
    }
  }

  const int otsu = BinaryThreshold::otsuThreshold(gray);
  const int peak = BinaryThreshold::peakThreshold(gray);
  BOOST_CHECK_GT(otsu, 30);
  BOOST_CHECK_LE(otsu, 220);
  BOOST_CHECK_GT(peak, 30);
  BOOST_CHECK_LE(peak, 220);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
