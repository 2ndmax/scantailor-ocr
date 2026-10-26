// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BrightnessFilter.h"

#include <QImage>
#include <algorithm>
#include <cstdint>

#include "ClampToByte.h"
#include "GrayImage.h"
#include "ParallelFor.h"

namespace imageproc {
void filterBrightnessInPlace(QImage& image, const std::function<void(GrayImage&)>& filter) {
  if (image.isNull()) {
    return;
  }
  const int depth = image.depth();
  if ((depth != 8) && (depth != 32)) {
    return;
  }

  const GrayImage gray(image);
  GrayImage filtered(gray);
  filter(filtered);

  const int w = image.width();
  uint8_t* const bits = image.bits();
  // 64 bits: with more than 2 GiB per image, y * stride overflows an int.
  const qsizetype stride = image.bytesPerLine();
  const uint8_t* const grayData = gray.data();
  const uint8_t* const filteredData = filtered.data();
  parallelFor(image.height(), [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      uint8_t* const line = bits + y * stride;
      const uint8_t* const grayLine = grayData + y * qsizetype(gray.stride());
      const uint8_t* const filteredLine = filteredData + y * qsizetype(filtered.stride());
      if (depth == 8) {
        // As in wienerColorFilterInPlace(), 8-bit images carry gray levels directly.
        std::copy(filteredLine, filteredLine + w, line);
      } else {
        auto* pixels = reinterpret_cast<QRgb*>(line);
        for (int x = 0; x < w; ++x) {
          // Shift the brightness and scale the deviation of each channel from it,
          // so that colours keep their hue.
          const float origin = grayLine[x];
          const float target = filteredLine[x];
          const float scale = (target + 1.0f) / (origin + 1.0f);
          const QRgb px = pixels[x];
          pixels[x] = qRgba(clampToByte(target + (qRed(px) - origin) * scale),
                            clampToByte(target + (qGreen(px) - origin) * scale),
                            clampToByte(target + (qBlue(px) - origin) * scale), qAlpha(px));
        }
      }
    }
  });
}
}  // namespace imageproc
