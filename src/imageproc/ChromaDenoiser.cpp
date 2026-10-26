// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ChromaDenoiser.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

#include "ClampToByte.h"
#include "GaussBlur.h"
#include "GrayImage.h"
#include "ParallelFor.h"

namespace imageproc {
namespace {
// JPEG (ITU-R BT.601) weights and colour difference scales.
constexpr float kR = 0.299f;
constexpr float kG = 0.587f;
constexpr float kB = 0.114f;
constexpr float kCb = 0.564f;
constexpr float kCr = 0.713f;
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
  // Rows and the two channels are independent, so they are computed in parallel.
  const uchar* const src = image.constBits();
  const qsizetype srcStride = image.bytesPerLine();
  uint8_t* const cbData = cb.data();
  uint8_t* const crData = cr.data();
  parallelFor(h, [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      const auto* pixels = reinterpret_cast<const QRgb*>(src + y * srcStride);
      uint8_t* cbLine = cbData + y * cb.stride();
      uint8_t* crLine = crData + y * cr.stride();
      for (int x = 0; x < w; ++x) {
        const float r = qRed(pixels[x]);
        const float g = qGreen(pixels[x]);
        const float b = qBlue(pixels[x]);
        const float luma = kR * r + kG * g + kB * b;
        cbLine[x] = clampToByte(128.0f + kCb * (b - luma));
        crLine[x] = clampToByte(128.0f + kCr * (r - luma));
      }
    }
  });

  const auto sigma = static_cast<float>(radius);
  // Both channels at the same time, if the thread budget allows it.
  GrayImage cbBlurred;
  GrayImage crBlurred;
  parallelFor(
      2,
      [&](const int begin, const int end) {
        for (int channel = begin; channel < end; ++channel) {
          if (channel == 0) {
            cbBlurred = gaussBlur(cb, sigma, sigma);
          } else {
            crBlurred = gaussBlur(cr, sigma, sigma);
          }
        }
      },
      1);

  const auto limit = static_cast<float>(grayLimit);
  uchar* const dst = image.bits();
  const qsizetype dstStride = image.bytesPerLine();
  const uint8_t* const cbBlurredData = std::as_const(cbBlurred).data();
  const uint8_t* const crBlurredData = crBlurred.data();
  parallelFor(h, [&](const int yBegin, const int yEnd) {
    for (int y = yBegin; y < yEnd; ++y) {
      auto* pixels = reinterpret_cast<QRgb*>(dst + y * dstStride);
      const uint8_t* cbLine = cbBlurredData + y * cbBlurred.stride();
      const uint8_t* crLine = crBlurredData + y * crBlurred.stride();
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
  });
}
}  // namespace imageproc
