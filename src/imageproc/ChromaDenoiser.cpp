// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ChromaDenoiser.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "GaussBlur.h"
#include "GrayImage.h"

namespace imageproc {
namespace {
// JPEG (ITU-R BT.601) weights and colour difference scales.
constexpr float kR = 0.299f;
constexpr float kG = 0.587f;
constexpr float kB = 0.114f;
constexpr float kCb = 0.564f;
constexpr float kCr = 0.713f;

uint8_t clampToByte(const float val) {
  return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f) + 0.5f);
}
}  // namespace

void chromaDenoiseInPlace(QImage& image, const int radius, const int grayLimit) {
  if (image.isNull() || (image.depth() != 32) || (radius < 1) || (grayLimit < 0)) {
    return;
  }
  const int w = image.width();
  const int h = image.height();

  // Colour differences, stored around 128.
  GrayImage cb(QSize(w, h));
  GrayImage cr(QSize(w, h));
  for (int y = 0; y < h; ++y) {
    const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    uint8_t* cbLine = cb.data() + y * cb.stride();
    uint8_t* crLine = cr.data() + y * cr.stride();
    for (int x = 0; x < w; ++x) {
      const float r = qRed(pixels[x]);
      const float g = qGreen(pixels[x]);
      const float b = qBlue(pixels[x]);
      const float luma = kR * r + kG * g + kB * b;
      cbLine[x] = clampToByte(128.0f + kCb * (b - luma));
      crLine[x] = clampToByte(128.0f + kCr * (r - luma));
    }
  }

  const auto sigma = static_cast<float>(radius);
  const GrayImage cbBlurred = gaussBlur(cb, sigma, sigma);
  const GrayImage crBlurred = gaussBlur(cr, sigma, sigma);

  const auto limit = static_cast<float>(grayLimit);
  for (int y = 0; y < h; ++y) {
    auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
    const uint8_t* cbLine = cbBlurred.data() + y * cbBlurred.stride();
    const uint8_t* crLine = crBlurred.data() + y * crBlurred.stride();
    for (int x = 0; x < w; ++x) {
      const QRgb px = pixels[x];
      const float luma = kR * qRed(px) + kG * qGreen(px) + kB * qBlue(px);
      float dCb = static_cast<float>(cbLine[x]) - 128.0f;
      float dCr = static_cast<float>(crLine[x]) - 128.0f;
      if (limit > 0.0f) {
        // Below the limit gray, above twice the limit unchanged, a smooth transition between.
        const float strength = std::sqrt(dCb * dCb + dCr * dCr);
        const float keep = std::clamp((strength - limit) / limit, 0.0f, 1.0f);
        dCb *= keep;
        dCr *= keep;
      }
      const float r = luma + dCr / kCr;
      const float b = luma + dCb / kCb;
      const float g = (luma - kR * r - kB * b) / kG;
      pixels[x] = qRgba(clampToByte(r), clampToByte(g), clampToByte(b), qAlpha(px));
    }
  }
}
}  // namespace imageproc
