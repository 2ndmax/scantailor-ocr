// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SubtractBackground.h"

#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "BrightnessFilter.h"
#include "GrayImage.h"

namespace imageproc {
namespace {
// The median is computed on a reduced image whose window radius is about this
// big; the background varies slowly, so nothing is lost, and large radii stay fast.
constexpr int kReducedRadius = 8;

GrayImage reduce(const GrayImage& src, const int factor) {
  const int w = src.width();
  const int h = src.height();
  GrayImage dst(QSize((w + factor - 1) / factor, (h + factor - 1) / factor));
  const uint8_t* const srcData = src.data();
  const int srcStride = src.stride();
  uint8_t* dstLine = dst.data();
  const int dstStride = dst.stride();
  for (int dy = 0; dy < dst.height(); ++dy) {
    const int y0 = dy * factor;
    const int y1 = std::min(y0 + factor, h);
    for (int dx = 0; dx < dst.width(); ++dx) {
      const int x0 = dx * factor;
      const int x1 = std::min(x0 + factor, w);
      unsigned sum = 0;
      for (int y = y0; y < y1; ++y) {
        const uint8_t* line = srcData + y * srcStride;
        for (int x = x0; x < x1; ++x) {
          sum += line[x];
        }
      }
      const unsigned count = (y1 - y0) * (x1 - x0);
      dstLine[dx] = static_cast<uint8_t>((sum + count / 2) / count);
    }
    dstLine += dstStride;
  }
  return dst;
}

GrayImage median(const GrayImage& src, const int radius) {
  const int w = src.width();
  const int h = src.height();
  GrayImage dst(src.size());
  const uint8_t* const srcData = src.data();
  const int srcStride = src.stride();
  uint8_t* dstLine = dst.data();
  const int dstStride = dst.stride();
  // 1-based rank of the median within the window.
  const unsigned rank = ((2 * radius + 1) * (2 * radius + 1) + 1) / 2;
  const auto clampX = [w](int x) { return std::clamp(x, 0, w - 1); };
  const auto clampY = [h](int y) { return std::clamp(y, 0, h - 1); };

  std::array<unsigned, 256> hist;
  for (int y = 0; y < h; ++y) {
    hist.fill(0);
    for (int yf = y - radius; yf <= y + radius; ++yf) {
      const uint8_t* line = srcData + clampY(yf) * srcStride;
      for (int xf = -radius; xf <= radius; ++xf) {
        ++hist[line[clampX(xf)]];
      }
    }
    int med = 0;
    unsigned below = 0;  // The number of window pixels darker than med.
    for (int x = 0; x < w; ++x) {
      if (x > 0) {
        const int xOut = clampX(x - radius - 1);
        const int xIn = clampX(x + radius);
        for (int yf = y - radius; yf <= y + radius; ++yf) {
          const uint8_t* line = srcData + clampY(yf) * srcStride;
          const uint8_t out = line[xOut];
          const uint8_t in = line[xIn];
          --hist[out];
          ++hist[in];
          below += (in < med) - (out < med);
        }
      }
      while (below + hist[med] < rank) {
        below += hist[med];
        ++med;
      }
      while (below >= rank) {
        --med;
        below -= hist[med];
      }
      dstLine[x] = static_cast<uint8_t>(med);
    }
    dstLine += dstStride;
  }
  return dst;
}

// Bilinear interpolation of the reduced image back to the full size.
GrayImage enlarge(const GrayImage& src, const QSize& size, const int factor) {
  const int sw = src.width();
  const int sh = src.height();
  GrayImage dst(size);
  const uint8_t* const srcData = src.data();
  const int srcStride = src.stride();
  uint8_t* dstLine = dst.data();
  const int dstStride = dst.stride();

  std::vector<int> x0(size.width());
  std::vector<int> x1(size.width());
  std::vector<float> fx(size.width());
  for (int x = 0; x < size.width(); ++x) {
    const float sx = std::clamp((x + 0.5f) / factor - 0.5f, 0.0f, static_cast<float>(sw - 1));
    x0[x] = static_cast<int>(sx);
    x1[x] = std::min(x0[x] + 1, sw - 1);
    fx[x] = sx - x0[x];
  }
  for (int y = 0; y < size.height(); ++y) {
    const float sy = std::clamp((y + 0.5f) / factor - 0.5f, 0.0f, static_cast<float>(sh - 1));
    const int y0 = static_cast<int>(sy);
    const int y1 = std::min(y0 + 1, sh - 1);
    const float fy = sy - y0;
    const uint8_t* line0 = srcData + y0 * srcStride;
    const uint8_t* line1 = srcData + y1 * srcStride;
    for (int x = 0; x < size.width(); ++x) {
      const float top = line0[x0[x]] + (line0[x1[x]] - line0[x0[x]]) * fx[x];
      const float bottom = line1[x0[x]] + (line1[x1[x]] - line1[x0[x]]) * fx[x];
      dstLine[x] = static_cast<uint8_t>(top + (bottom - top) * fy + 0.5f);
    }
    dstLine += dstStride;
  }
  return dst;
}

GrayImage estimateBackground(const GrayImage& image, const int radius) {
  const int factor = std::max(1, radius / kReducedRadius);
  if (factor == 1) {
    return median(image, radius);
  }
  const int reducedRadius = std::max(1, static_cast<int>(std::lround(static_cast<double>(radius) / factor)));
  return enlarge(median(reduce(image, factor), reducedRadius), image.size(), factor);
}

uint8_t clampToByte(const float val) {
  return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f) + 0.5f);
}

// Divides by the background, which takes it to white and keeps dark text dark.
void whitenInPlace(GrayImage& channel, const int radius, const double coef) {
  const GrayImage background = estimateBackground(channel, radius);
  const float strength = static_cast<float>(std::min(coef, 1.0));
  uint8_t* line = channel.data();
  const int stride = channel.stride();
  const uint8_t* bgLine = background.data();
  const int bgStride = background.stride();
  for (int y = 0; y < channel.height(); ++y) {
    for (int x = 0; x < channel.width(); ++x) {
      const float origin = line[x];
      const float white = origin * 255.0f / std::max<float>(bgLine[x], 1.0f);
      line[x] = clampToByte(origin + strength * (white - origin));
    }
    line += stride;
    bgLine += bgStride;
  }
}

void whitenColorInPlace(QImage& image, const int radius, const double coef) {
  const int w = image.width();
  const int h = image.height();
  std::array<GrayImage, 3> channels;
  for (int c = 0; c < 3; ++c) {
    channels[c] = GrayImage(QSize(w, h));
  }
  for (int y = 0; y < h; ++y) {
    const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < w; ++x) {
      channels[0].data()[y * channels[0].stride() + x] = static_cast<uint8_t>(qRed(pixels[x]));
      channels[1].data()[y * channels[1].stride() + x] = static_cast<uint8_t>(qGreen(pixels[x]));
      channels[2].data()[y * channels[2].stride() + x] = static_cast<uint8_t>(qBlue(pixels[x]));
    }
  }
  // The channels are independent, so they are computed at the same time.
  std::thread green([&] { whitenInPlace(channels[1], radius, coef); });
  std::thread blue([&] { whitenInPlace(channels[2], radius, coef); });
  whitenInPlace(channels[0], radius, coef);
  green.join();
  blue.join();
  for (int y = 0; y < h; ++y) {
    auto* pixels = reinterpret_cast<QRgb*>(image.scanLine(y));
    const uint8_t* r = channels[0].data() + y * channels[0].stride();
    const uint8_t* g = channels[1].data() + y * channels[1].stride();
    const uint8_t* b = channels[2].data() + y * channels[2].stride();
    for (int x = 0; x < w; ++x) {
      pixels[x] = qRgba(r[x], g[x], b[x], qAlpha(pixels[x]));
    }
  }
}
}  // namespace

void subtractBackgroundInPlace(GrayImage& image, const int radius, const double coef) {
  if (image.isNull() || (radius < 1) || (coef <= 0.0)) {
    return;
  }
  const GrayImage background = estimateBackground(image, radius);

  const int w = image.width();
  const int h = image.height();
  const uint8_t* bgLine = background.data();
  const int bgStride = background.stride();
  uint8_t brightest = 0;
  for (int y = 0; y < h; ++y) {
    brightest = std::max(brightest, *std::max_element(bgLine, bgLine + w));
    bgLine += bgStride;
  }

  const float strength = static_cast<float>(std::min(coef, 1.0));
  uint8_t* line = image.data();
  const int stride = image.stride();
  bgLine = background.data();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      line[x] = clampToByte(line[x] + strength * (brightest - bgLine[x]));
    }
    line += stride;
    bgLine += bgStride;
  }
}

void subtractBackgroundColorInPlace(QImage& image, const int radius, const double coef, const bool whitePaper) {
  if (image.isNull() || (radius < 1) || (coef <= 0.0)) {
    return;
  }
  const int depth = image.depth();
  if ((depth != 8) && (depth != 32)) {
    return;
  }
  if (whitePaper && (depth == 32)) {
    whitenColorInPlace(image, radius, coef);
    return;
  }

  filterBrightnessInPlace(image, [&](GrayImage& gray) {
    if (whitePaper) {
      whitenInPlace(gray, radius, coef);
    } else {
      subtractBackgroundInPlace(gray, radius, coef);
    }
  });
}
}  // namespace imageproc
