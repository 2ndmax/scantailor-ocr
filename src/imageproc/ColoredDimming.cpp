// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColoredDimming.h"

#include <QImage>
#include <algorithm>
#include <cmath>

#include "ClampToByte.h"
#include "ParallelFor.h"

namespace imageproc {
QImage dimColoredPixels(const QImage& image, const double coef) {
  if (image.isNull() || (coef == 0.0) || image.allGray()) {
    return image;
  }
  QImage result = image.convertToFormat(QImage::Format_RGB32);
  const auto strength = static_cast<float>(std::abs(coef) / 255.0);
  const bool darken = coef > 0.0;
  const int width = result.width();
  // The copy may still share its data with the source; it is detached here, once, as detaching
  // from several threads at the same time would race.
  uchar* const data = result.bits();
  const qsizetype stride = result.bytesPerLine();
  parallelFor(result.height(), [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      auto* const line = reinterpret_cast<QRgb*>(data + y * stride);
      for (int x = 0; x < width; ++x) {
        const QRgb pixel = line[x];
        const int r = qRed(pixel);
        const int g = qGreen(pixel);
        const int b = qBlue(pixel);
        const int saturation = std::max({r, g, b}) - std::min({r, g, b});
        if (saturation == 0) {
          continue;
        }
        const float t = std::min(1.0f, strength * static_cast<float>(saturation));
        const auto move = [&](const int c) {
          const float value = darken ? static_cast<float>(c) * (1.0f - t)
                                     : static_cast<float>(c) + (255.0f - static_cast<float>(c)) * t;
          return clampToByte(value);
        };
        line[x] = qRgb(move(r), move(g), move(b));
      }
    }
  });
  return result;
}
}  // namespace imageproc
