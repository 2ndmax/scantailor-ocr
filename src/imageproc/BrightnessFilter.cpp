// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BrightnessFilter.h"

#include <QImage>
#include <algorithm>
#include <cstdint>
#include <utility>

#include "BadAllocIfNull.h"
#include "ClampToByte.h"
#include "GrayImage.h"
#include "ParallelFor.h"

namespace imageproc {
namespace {
bool isIdentityGrayscale(const QImage& image) {
  if (image.format() == QImage::Format_Grayscale8) {
    return true;
  }
  if ((image.format() != QImage::Format_Indexed8) || !image.isGrayscale()) {
    return false;
  }
  const QVector<QRgb> palette = image.colorTable();
  if (palette.size() != 256) {
    return false;
  }
  for (int i = 0; i < 256; ++i) {
    if (palette[i] != qRgb(i, i, i)) {
      return false;
    }
  }
  return true;
}
}  // namespace

void filterBrightnessInPlace(QImage& image, const std::function<void(GrayImage&)>& filter) {
  if (image.isNull()) {
    return;
  }

  const GrayImage gray(image);
  GrayImage filtered(gray);
  filter(filtered);

  const int w = image.width();
  const int h = image.height();
  const uint8_t* const grayData = gray.data();
  const uint8_t* const filteredData = filtered.data();

  if (isIdentityGrayscale(image)) {
    uint8_t* const bits = image.bits();
    // 64 bits: with more than 2 GiB per image, y * stride overflows an int.
    const qsizetype stride = image.bytesPerLine();
    parallelFor(h, [&](const int yBegin, const int yEnd) {
      for (int y = yBegin; y < yEnd; ++y) {
        uint8_t* const line = bits + y * stride;
        const uint8_t* const filteredLine = filteredData + y * qsizetype(filtered.stride());
        std::copy(filteredLine, filteredLine + w, line);
      }
    });
    return;
  }

  const bool native32 = (image.format() == QImage::Format_RGB32) || (image.format() == QImage::Format_ARGB32);
  QImage converted;
  if (!native32) {
    const QImage::Format fmt = image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    converted = image.convertToFormat(fmt);
    badAllocIfNull(converted);
  }
  QImage& dstImage = native32 ? image : converted;

  uint8_t* const bits = dstImage.bits();
  const qsizetype stride = dstImage.bytesPerLine();
  parallelFor(h, [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      auto* const pixels = reinterpret_cast<QRgb*>(bits + y * stride);
      const uint8_t* const grayLine = grayData + y * qsizetype(gray.stride());
      const uint8_t* const filteredLine = filteredData + y * qsizetype(filtered.stride());
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
  });

  if (!native32) {
    image = std::move(converted);
  }
}
}  // namespace imageproc
