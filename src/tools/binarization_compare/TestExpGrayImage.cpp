// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <QSize>
#include <algorithm>
#include <exception>
#include <iostream>

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
}  // namespace

int main() {
  try {
    testEqualizeSmallImages();
    testRetinexClamping();
  } catch (const std::exception& e) {
    std::cerr << "Test failed: " << e.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "Test failed with unknown exception\n";
    return 1;
  }
  return 0;
}
