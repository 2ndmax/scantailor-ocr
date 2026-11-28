// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DEWARPING_GRIDUTILS_H_
#define SCANTAILOR_DEWARPING_GRIDUTILS_H_

#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Grid.h"
#include "VecNT.h"

namespace dewarping {

/**
 * \brief Bilinearly samples a grid value at \p pos, returning \p defaultValue out of bounds.
 */
template <typename Node, typename Extractor>
float sampleGridBilinear(const Grid<Node>& grid, Extractor extractor, const Vec2f& pos, float defaultValue) {
  const auto xBase = static_cast<float>(std::floor(pos[0]));
  const auto yBase = static_cast<float>(std::floor(pos[1]));
  const auto xBaseI = static_cast<int>(xBase);
  const auto yBaseI = static_cast<int>(yBase);

  if ((xBaseI < 0) || (yBaseI < 0) || (xBaseI + 1 >= grid.width()) || (yBaseI + 1 >= grid.height())) {
    return defaultValue;
  }

  const float x = pos[0] - xBase;
  const float y = pos[1] - yBase;
  const float x1 = 1.0f - x;
  const float y1 = 1.0f - y;

  const int stride = grid.stride();
  const Node* base = grid.data() + yBaseI * stride + xBaseI;
  return extractor(base[0]) * x1 * y1 + extractor(base[1]) * x * y1 + extractor(base[stride]) * x1 * y
         + extractor(base[stride + 1]) * x * y;
}

/**
 * \brief Renders a scalar grid overlay (signed red/blue or unsigned red) over \p background or white.
 */
template <typename Node, typename Extractor>
QImage visualizeGridOverlay(const Grid<Node>& grid,
                            Extractor extractor,
                            const QImage* background = nullptr,
                            bool splitSignColors = true) {
  const int width = grid.width();
  const int height = grid.height();
  if ((width <= 0) || (height <= 0)) {
    return QImage();
  }

  const int gridStride = grid.stride();
  float minValue = std::numeric_limits<float>::max();
  float maxValue = std::numeric_limits<float>::lowest();

  const Node* gridLine = grid.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = extractor(gridLine[x]);
      if (value < minValue) {
        minValue = value;
      }
      if (value > maxValue) {
        maxValue = value;
      }
    }
    gridLine += gridStride;
  }

  float scale = std::max(maxValue, -minValue);
  if (scale > std::numeric_limits<float>::epsilon()) {
    scale = 255.0f / scale;
  }

  QImage overlay(width, height, QImage::Format_ARGB32_Premultiplied);
  auto* overlayLine = reinterpret_cast<uint32_t*>(overlay.bits());
  const int overlayStride = overlay.bytesPerLine() / 4;

  gridLine = grid.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = extractor(gridLine[x]) * scale;
      const int magnitude = qBound(0, static_cast<int>(std::round(std::fabs(value))), 255);
      if (!splitSignColors || (value > 0)) {
        overlayLine[x] = qRgba(magnitude, 0, 0, magnitude);
      } else {
        overlayLine[x] = qRgba(0, 0, magnitude, magnitude);
      }
    }
    gridLine += gridStride;
    overlayLine += overlayStride;
  }

  QImage canvas;
  if (background) {
    canvas = background->convertToFormat(QImage::Format_ARGB32_Premultiplied);
  } else {
    canvas = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(0xffffffff);
  }

  QPainter painter(&canvas);
  painter.drawImage(0, 0, overlay);
  return canvas;
}

}  // namespace dewarping

#endif  // SCANTAILOR_DEWARPING_GRIDUTILS_H_
