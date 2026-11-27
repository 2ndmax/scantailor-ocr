// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QImage>
#include <QSize>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <limits>

#include "BinaryImage.h"
#include "ExpBinarize.h"
#include "ExpColorFilter.h"
#include "ExpGrayImage.h"
#include "GrayImage.h"

namespace {
namespace stexp = imageproc::stexp;

void require(const bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void testEqualizeSmallImages() {
  imageproc::GrayImage img(QSize(10, 10));
  require(!img.isNull(), "small image allocation failed");
  for (int y = 0; y < img.height(); ++y) {
    unsigned char* line = img.data() + y * img.stride();
    for (int x = 0; x < img.width(); ++x) {
      line[x] = static_cast<unsigned char>(y * 10 + x);
    }
  }

  const imageproc::GrayImage equalized = stexp::grayEqualize(img, 1, 1.0f, false);
  require(!equalized.isNull(), "grayEqualize returned null on < 256 px image");
  require(equalized.size() == QSize(10, 10), "grayEqualize changed image size");

  int minVal = 255;
  int maxVal = 0;
  for (int y = 0; y < equalized.height(); ++y) {
    const unsigned char* line = equalized.data() + y * equalized.stride();
    for (int x = 0; x < equalized.width(); ++x) {
      minVal = std::min(minVal, static_cast<int>(line[x]));
      maxVal = std::max(maxVal, static_cast<int>(line[x]));
    }
  }
  require(minVal < 20, "grayEqualize failed to stretch dark pixels");
  require(maxVal > 230, "grayEqualize failed to stretch bright pixels");

  const imageproc::GrayImage screened = stexp::grayScreen(img, 1, 1.0f);
  require(!screened.isNull(), "grayScreen returned null on < 256 px image");
  require(screened.size() == QSize(10, 10), "grayScreen changed image size");
}

void testRetinexClamping() {
  {
    imageproc::GrayImage img(QSize(32, 32));
    require(!img.isNull(), "black image allocation failed");
    img.fill(0);
    img.data()[16 * img.stride() + 16] = 255;

    const imageproc::GrayImage out = stexp::grayRetinex(img, 5, 1.0f);
    require(!out.isNull(), "grayRetinex returned null");
    const unsigned char center = out.data()[16 * out.stride() + 16];
    require(center == 255, "grayRetinex wrapped center 255 pixel on black background");
  }

  {
    imageproc::GrayImage img(QSize(32, 32));
    require(!img.isNull(), "uniform image allocation failed");
    img.fill(10);
    img.data()[16 * img.stride() + 16] = 23;

    const imageproc::GrayImage out = stexp::grayRetinex(img, 5, 1.0f);
    require(!out.isNull(), "grayRetinex returned null");
    const unsigned char center = out.data()[16 * out.stride() + 16];
    require(center == 255, "grayRetinex wrapped center 23 pixel on gray 10 background");
  }
}

void testGatosBgLargeImageNoOverflow() {
  imageproc::GrayImage img(QSize(4200, 4200));
  require(!img.isNull(), "large gray image allocation failed");
  img.fill(250);

  imageproc::BinaryImage niblack(QSize(4200, 4200), imageproc::WHITE);
  require(!niblack.isNull(), "large niblack image allocation failed");
  niblack.fill(QRect(2100, 2100, 1, 1), imageproc::BLACK);

  const imageproc::GrayImage out = stexp::binarizeGatosBG(img, niblack, 2100);
  require(!out.isNull(), "binarizeGatosBG returned null on large image");
  require(out.size() == QSize(4200, 4200), "binarizeGatosBG changed large image size");
  require(out.data()[2100 * out.stride() + 2100] == 250, "binarizeGatosBG overflowed 32-bit sum on >16.8 MP window");
}

void testGatosBgAllBlackNiblack() {
  imageproc::GrayImage img(QSize(512, 512));
  require(!img.isNull(), "gray image allocation failed");
  img.fill(123);

  const imageproc::BinaryImage niblack(QSize(512, 512), imageproc::BLACK);
  require(!niblack.isNull(), "all-black niblack allocation failed");

  const imageproc::GrayImage out = stexp::binarizeGatosBG(img, niblack, 1);
  require(!out.isNull(), "binarizeGatosBG returned null on all-black niblack");
  require(out.size() == QSize(512, 512), "binarizeGatosBG changed image size on all-black niblack");
  require(out.data()[0] == 123, "unexpected top-left pixel on all-black niblack");
  require(out.data()[256 * out.stride() + 256] == 123, "unexpected center pixel on all-black niblack");
  require(out.data()[511 * out.stride() + 511] == 123, "unexpected bottom-right pixel on all-black niblack");
}

void testHsvKMeansBoundsAndCoefBg() {
  QImage img(16, 16, QImage::Format_RGB32);
  require(!img.isNull(), "color test image allocation failed");
  for (int y = 0; y < img.height(); ++y) {
    QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      row[x] = qRgb(x * 16, y * 16, ((x + y) * 8) & 255);
    }
  }

  imageproc::BinaryImage mask(img.size(), imageproc::WHITE);
  mask.fill(QRect(0, 0, 8, 16), imageproc::BLACK);
  const imageproc::BinaryImage maskZones(img.size(), imageproc::BLACK);

  struct Case {
    float coefBg;
    float coefNorm;
  };
  const Case cases[] = {
      {0.0f, 0.0f},
      {1.0f, 0.0f},
      {-0.5f, 0.0f},
      {1.5f, 0.0f},
      {std::numeric_limits<float>::quiet_NaN(), 0.0f},
      {0.0f, std::numeric_limits<float>::quiet_NaN()},
  };

  for (const Case& c : cases) {
    QImage dst = img.copy();
    const double mse
        = stexp::hsvKMeansInPlace(dst, img, mask, maskZones, 254, 128, 0, 0.0f, c.coefNorm, c.coefBg, true, true);
    require(!dst.isNull(), "hsvKMeansInPlace returned null image");
    require(dst.size() == img.size(), "hsvKMeansInPlace changed image size");
    if (std::isfinite(c.coefNorm)) {
      require(std::isfinite(mse) && mse >= 0.0, "hsvKMeansInPlace produced non-finite or negative mse");
    }
  }
}

void testHsvKMeansEmptyMaskAndZones() {
  QImage img(16, 16, QImage::Format_RGB32);
  require(!img.isNull(), "empty-mask test image allocation failed");
  for (int y = 0; y < img.height(); ++y) {
    QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
    for (int x = 0; x < img.width(); ++x) {
      row[x] = qRgb(10 + x * 8, 20 + y * 8, 200);
    }
  }

  // Case 1: All-white mask (no foreground pixels) on full zone mask.
  {
    const imageproc::BinaryImage mask(img.size(), imageproc::WHITE);
    const imageproc::BinaryImage maskZones(img.size(), imageproc::BLACK);
    QImage dst = img.copy();
    const double mse = stexp::hsvKMeansInPlace(dst, img, mask, maskZones, 6, 128, 0, 0.0f, 0.0f, 0.0f, true, true);
    require(std::isfinite(mse) && mse >= 0.0, "all-white mask produced invalid mse");
    for (int y = 0; y < dst.height(); ++y) {
      for (int x = 0; x < dst.width(); ++x) {
        require(dst.pixel(x, y) == qRgb(255, 255, 255), "expected white background output on all-white mask");
      }
    }
  }

  // Case 2: All-white zone mask (zero pixels inside mask_zones).
  {
    const imageproc::BinaryImage mask(img.size(), imageproc::BLACK);
    const imageproc::BinaryImage maskZones(img.size(), imageproc::WHITE);
    QImage dst(img.size(), QImage::Format_RGB32);
    dst.fill(qRgb(42, 84, 126));
    const double mse = stexp::hsvKMeansInPlace(dst, img, mask, maskZones, 6, 128, 0, 0.0f, 0.0f, 0.5f, true, true);
    require(mse == 0.0, "expected zero mse on empty mask_zones");
    require(dst.pixel(0, 0) == qRgb(42, 84, 126), "dst should remain untouched outside mask_zones");
  }

  // Case 3: Single foreground pixel in mask while fgcount == 6 (multiple empty clusters).
  {
    imageproc::BinaryImage mask(img.size(), imageproc::WHITE);
    mask.fill(QRect(3, 3, 1, 1), imageproc::BLACK);
    const imageproc::BinaryImage maskZones(img.size(), imageproc::BLACK);
    QImage dst = img.copy();
    const double mse = stexp::hsvKMeansInPlace(dst, img, mask, maskZones, 6, 128, 0, 0.0f, 0.0f, 0.0f, false, false);
    require(std::isfinite(mse) && mse >= 0.0, "single-pixel foreground mask produced invalid mse");
    require(dst.size() == img.size(), "single-pixel foreground mask changed dst size");
  }
}
}  // namespace

int main() {
  try {
    testEqualizeSmallImages();
    testRetinexClamping();
    testGatosBgLargeImageNoOverflow();
    testGatosBgAllBlackNiblack();
    testHsvKMeansBoundsAndCoefBg();
    testHsvKMeansEmptyMaskAndZones();
  } catch (const std::exception& e) {
    std::cerr << "Test failed: " << e.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception\n";
    return 1;
  }
  return 0;
}
