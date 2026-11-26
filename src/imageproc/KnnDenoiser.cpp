// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "KnnDenoiser.h"

#include <QImage>
#include <QRect>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "BrightnessFilter.h"
#include "ClampToByte.h"
#include "GrayImage.h"
#include "IntegralImage.h"
#include "ParallelFor.h"

namespace imageproc {
void knnDenoiseInPlace(GrayImage& image, const int radius, const double coef) {
  if (image.isNull() || (radius < 1) || !std::isfinite(coef) || !(coef > 0.0)) {
    return;
  }
  // Weights below this do not count as similar neighbourhoods.
  constexpr float kThresholdWeight = 0.02f;
  // With enough similar neighbourhoods the result leans to the average, otherwise to the pixel.
  constexpr float kThresholdLerp = 0.66f;
  constexpr float kNoiseLerp = 0.16f;

  const int w = image.width();
  const int h = image.height();
  uint8_t* const data = image.data();
  const ptrdiff_t stride = image.stride();

  IntegralImage<uint64_t> integral(w, h);
  for (int y = 0; y < h; ++y) {
    integral.beginRow();
    for (int x = 0; x < w; ++x) {
      integral.push(data[static_cast<ptrdiff_t>(y) * stride + x]);
    }
  }

  const int64_t side = 2LL * radius + 1;
  const float areaInv = 1.0f / static_cast<float>(side * side);
  const float noiseWeight = static_cast<float>(1.0 / (coef * coef));
  const float pixelWeight = 1.0f / 255.0f;
  // Each pixel only reads the integral image, so the rows can be computed in parallel.
  parallelFor(h, [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      uint8_t* const line = data + static_cast<ptrdiff_t>(y) * stride;
      for (int x = 0; x < w; ++x) {
        const float origin = line[x];
        float count = areaInv;
        float sumWeights = 1.0f;
        float color = origin;
        for (int r = 1; r <= radius; ++r) {
          const int left = std::max(x - r, 0);
          const int right = std::min(x + r, w - 1);
          const int top = std::max(y - r, 0);
          const int bottom = std::min(y + r, h - 1);
          const QRect rect(left, top, right - left + 1, bottom - top + 1);
          const float mean = static_cast<float>(integral.sum(rect))
                             / static_cast<float>(static_cast<int64_t>(rect.width()) * rect.height());
          const float delta = (origin - mean) * pixelWeight * static_cast<float>(r);
          const float weightF
              = std::exp(-(static_cast<float>(static_cast<int64_t>(r) * r) * areaInv + delta * delta * noiseWeight));
          const auto weightR = static_cast<float>(8 * r);
          color += mean * weightF * weightR;
          sumWeights += weightF * weightR;
          if (weightF > kThresholdWeight) {
            count += areaInv * weightR;
          }
        }
        color /= sumWeights;
        const float lerp = (count > kThresholdLerp) ? kNoiseLerp : (1.0f - kNoiseLerp);
        color += (origin - color) * lerp;
        line[x] = clampToByte(color);
      }
    }
  });
}

void knnDenoiseColorInPlace(QImage& image, const int radius, const double coef) {
  if ((radius < 1) || !std::isfinite(coef) || !(coef > 0.0)) {
    return;
  }
  filterBrightnessInPlace(image, [&](GrayImage& gray) { knnDenoiseInPlace(gray, radius, coef); });
}
}  // namespace imageproc
