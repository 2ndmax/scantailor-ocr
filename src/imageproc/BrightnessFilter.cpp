// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BrightnessFilter.h"

#include <QImage>
#include <algorithm>
#include <cstdint>

#include "GrayImage.h"

namespace imageproc {
namespace {
uint8_t clampToByte(const float val) {
  return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f) + 0.5f);
}
}  // namespace

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
  const int h = image.height();
  uint8_t* line = image.bits();
  const int stride = image.bytesPerLine();
  const uint8_t* grayLine = gray.data();
  const uint8_t* filteredLine = filtered.data();
  const int grayStride = gray.stride();
  const int filteredStride = filtered.stride();
  for (int y = 0; y < h; ++y) {
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
    line += stride;
    grayLine += grayStride;
    filteredLine += filteredStride;
  }
}
}  // namespace imageproc
