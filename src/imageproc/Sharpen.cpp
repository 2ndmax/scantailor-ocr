// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Sharpen.h"

#include <QImage>
#include <algorithm>
#include <cstdint>

#include "BrightnessFilter.h"
#include "GaussBlur.h"
#include "GrayImage.h"

namespace imageproc {
void sharpenInPlace(GrayImage& image, const int radius, const double coef) {
  if (image.isNull() || (radius < 1) || (coef <= 0.0)) {
    return;
  }
  const auto sigma = static_cast<float>(radius);
  const GrayImage blurred = gaussBlur(image, sigma, sigma);
  const auto amount = static_cast<float>(coef);

  uint8_t* line = image.data();
  const int stride = image.stride();
  const uint8_t* blurLine = blurred.data();
  const int blurStride = blurred.stride();
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const float origin = line[x];
      const float val = origin + amount * (origin - static_cast<float>(blurLine[x]));
      line[x] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f) + 0.5f);
    }
    line += stride;
    blurLine += blurStride;
  }
}

void sharpenColorInPlace(QImage& image, const int radius, const double coef) {
  if ((radius < 1) || (coef <= 0.0)) {
    return;
  }
  filterBrightnessInPlace(image, [&](GrayImage& gray) { sharpenInPlace(gray, radius, coef); });
}
}  // namespace imageproc
