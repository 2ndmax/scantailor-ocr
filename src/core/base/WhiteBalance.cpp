// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.
// Taken from ScanTailor Spectre (src/core); debug output and unused functions removed.

#include "WhiteBalance.h"

#include <algorithm>
#include <cmath>
#include <vector>

QColor WhiteBalance::estimateBackgroundColor(const QImage& image, const QRect& region) {
  if (image.isNull()) {
    return QColor();
  }

  QRect r = region.isValid() ? region.intersected(image.rect()) : image.rect();
  if (r.width() < 16 || r.height() < 16) {
    r = image.rect();
  }
  if (r.width() < 16 || r.height() < 16) {
    return QColor();
  }

  // Coarse deterministic grid: cap the number of sampled pixels so this stays
  // cheap even on full-resolution scans.
  const int targetSamples = 32768;
  const double area = static_cast<double>(r.width()) * r.height();
  const int step = std::max(1, static_cast<int>(std::sqrt(area / targetSamples)));

  struct Sample {
    int r, g, b, brightness;
  };
  std::vector<Sample> samples;
  samples.reserve(targetSamples + 1024);

  // Minimum brightness for a pixel to plausibly be paper rather than ink.
  const int minBackgroundBrightness = 96;

  const QImage::Format fmt = image.format();
  const bool directAccess
      = (fmt == QImage::Format_RGB32 || fmt == QImage::Format_ARGB32 || fmt == QImage::Format_ARGB32_Premultiplied);

  for (int y = r.top(); y <= r.bottom(); y += step) {
    const QRgb* line = directAccess ? reinterpret_cast<const QRgb*>(image.constScanLine(y)) : nullptr;
    for (int x = r.left(); x <= r.right(); x += step) {
      const QRgb pixel = line ? line[x] : image.pixel(x, y);
      const int red = qRed(pixel);
      const int green = qGreen(pixel);
      const int blue = qBlue(pixel);
      const int brightness = (red + green + blue) / 3;
      if (brightness < minBackgroundBrightness) {
        continue;  // Ink / dark content, not paper
      }
      samples.push_back({red, green, blue, brightness});
    }
  }

  if (samples.size() < 64) {
    return QColor();  // Not enough bright pixels to call anything "paper"
  }

  // Keep the brightest quartile - on a text page that is the paper itself,
  // not halftones or anti-aliased edges.
  const size_t keepCount = std::max<size_t>(32, samples.size() / 4);
  std::nth_element(samples.begin(), samples.begin() + keepCount, samples.end(),
                   [](const Sample& a, const Sample& b) { return a.brightness > b.brightness; });
  samples.resize(keepCount);

  std::vector<int> rValues, gValues, bValues;
  rValues.reserve(keepCount);
  gValues.reserve(keepCount);
  bValues.reserve(keepCount);
  for (const Sample& s : samples) {
    rValues.push_back(s.r);
    gValues.push_back(s.g);
    bValues.push_back(s.b);
  }
  std::sort(rValues.begin(), rValues.end());
  std::sort(gValues.begin(), gValues.end());
  std::sort(bValues.begin(), bValues.end());

  const size_t mid = keepCount / 2;
  const QColor result(rValues[mid], gValues[mid], bValues[mid]);

  // A background estimate darker than this is not paper (dark photo page).
  const int brightness = (result.red() + result.green() + result.blue()) / 3;
  if (brightness < 120) {
    return QColor();
  }
  return result;
}

bool WhiteBalance::hasSignificantCast(const QColor& paperColor, int threshold) {
  if (!paperColor.isValid()) {
    return false;
  }

  const int r = paperColor.red();
  const int g = paperColor.green();
  const int b = paperColor.blue();

  // Check deviation from neutral (where R == G == B)
  const int avg = (r + g + b) / 3;
  const int maxDev = std::max({std::abs(r - avg), std::abs(g - avg), std::abs(b - avg)});

  return maxDev >= threshold;
}

QImage WhiteBalance::apply(const QImage& image, const QColor& paperColor) {
  if (image.isNull() || !paperColor.isValid()) {
    return image;
  }

  // Don't correct if already neutral
  if (!hasSignificantCast(paperColor)) {
    return image;
  }

  const int paperR = paperColor.red();
  const int paperG = paperColor.green();
  const int paperB = paperColor.blue();

  // Avoid division by zero
  if (paperR == 0 || paperG == 0 || paperB == 0) {
    return image;
  }

  // Target a near-white neutral value so paper brightens toward white.
  const float targetNeutral = 245.0f;

  float rMult = targetNeutral / paperR;
  float gMult = targetNeutral / paperG;
  float bMult = targetNeutral / paperB;

  // Clamp multipliers to a reasonable range to prevent extreme shifts.
  auto clampGain = [](float v) { return std::clamp(v, 0.2f, 6.0f); };
  rMult = clampGain(rMult);
  gMult = clampGain(gMult);
  bMult = clampGain(bMult);


  // Apply correction to all pixels
  // Convert to RGB32 to guarantee QRgb* scanLine access is safe
  QImage result = image.convertToFormat(QImage::Format_RGB32);
  const int width = result.width();
  const int height = result.height();

  for (int y = 0; y < height; ++y) {
    // Safe cast: Format_RGB32 guarantees 32-bit QRgb per pixel
    QRgb* line = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const QRgb pixel = line[x];
      const int r = std::min(255, static_cast<int>(qRed(pixel) * rMult));
      const int g = std::min(255, static_cast<int>(qGreen(pixel) * gMult));
      const int b = std::min(255, static_cast<int>(qBlue(pixel) * bMult));
      line[x] = qRgb(r, g, b);
    }
  }

  return result;
}
