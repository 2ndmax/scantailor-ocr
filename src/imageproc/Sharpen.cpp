// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Sharpen.h"

#include <QImage>
#include <algorithm>
#include <cstdint>

#include "BrightnessFilter.h"
#include "ClampToByte.h"
#include "GaussBlur.h"
#include "GrayImage.h"
#include "ParallelFor.h"

namespace imageproc {
void sharpenInPlace(GrayImage& image, const int radius, const double coef) {
  if (image.isNull() || (radius < 1) || (coef <= 0.0)) {
    return;
  }
  const auto sigma = static_cast<float>(radius);
  const GrayImage blurred = gaussBlur(image, sigma, sigma);
  const auto amount = static_cast<float>(coef);

  const int w = image.width();
  uint8_t* const data = image.data();
  const int stride = image.stride();
  parallelFor(image.height(), [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      uint8_t* const line = data + y * stride;
      const uint8_t* const blurLine = blurred.data() + y * blurred.stride();
      for (int x = 0; x < w; ++x) {
        const float origin = line[x];
        const float val = origin + amount * (origin - static_cast<float>(blurLine[x]));
        line[x] = clampToByte(val);
      }
    }
  });
}

void sharpenColorInPlace(QImage& image, const int radius, const double coef) {
  if ((radius < 1) || (coef <= 0.0)) {
    return;
  }
  filterBrightnessInPlace(image, [&](GrayImage& gray) { sharpenInPlace(gray, radius, coef); });
}
}  // namespace imageproc
