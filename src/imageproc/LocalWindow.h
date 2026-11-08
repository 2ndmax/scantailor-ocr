// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_LOCALWINDOW_H_
#define SCANTAILOR_IMAGEPROC_LOCALWINDOW_H_

#include <QRect>
#include <QSize>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include "IntegralImage.h"

namespace imageproc {
/**
 * \brief The neighbourhood of a pixel that a local binarization method looks at.
 *
 * The two kinds of window differ for even sizes. They are kept apart on purpose: making them
 * the same would change the results of the methods.
 */
class LocalWindow {
 public:
  /**
   * The window of the classic methods. With an even size, it reaches one pixel further to the
   * left and to the top than to the right and to the bottom, the pixel included.
   */
  static LocalWindow ofSize(const QSize& size) {
    const int toLeft = size.width() >> 1;
    const int toTop = size.height() >> 1;
    return LocalWindow(toLeft, size.width() - toLeft, toTop, size.height() - toTop);
  }

  /** The window of the methods from ScanTailor Experimental: always 2 * radius + 1, centred. */
  static LocalWindow ofRadius(const int radius) { return LocalWindow(radius, radius + 1, radius, radius + 1); }

  int left(const int x) const { return std::max(0, x - m_toLeft); }

  int right(const int x, const int imageWidth) const { return std::min(imageWidth, x + m_toRight); }  // exclusive

  int top(const int y) const { return std::max(0, y - m_toTop); }

  int bottom(const int y, const int imageHeight) const { return std::min(imageHeight, y + m_toBottom); }  // exclusive

 private:
  LocalWindow(const int toLeft, const int toRight, const int toTop, const int toBottom)
      : m_toLeft(toLeft), m_toRight(toRight), m_toTop(toTop), m_toBottom(toBottom) {}

  int m_toLeft;
  int m_toRight;
  int m_toTop;
  int m_toBottom;
};

namespace local_window_detail {
// Calls visit(x, windowRect, windowArea) for every pixel, row by row, and endOfRow() after each row.
template <typename Visit, typename EndOfRow>
void forEachWindow(const LocalWindow& window, const int w, const int h, Visit visit, EndOfRow endOfRow) {
  for (int y = 0; y < h; ++y) {
    const int top = window.top(y);
    const int bottom = window.bottom(y, h);
    for (int x = 0; x < w; ++x) {
      const int left = window.left(x);
      const int right = window.right(x, w);
      const int area = (bottom - top) * (right - left);
      assert(area > 0);  // because the window and the image are not empty
      visit(x, QRect(left, top, right - left, bottom - top), area);
    }
    endOfRow();
  }
}
}  // namespace local_window_detail

/**
 * \brief Provides the mean gray level of the window around every pixel of a gray image.
 *
 * Results of the binarization methods depend on the exact floating point calculation here: the
 * order of the operations and the integer types must not change.
 */
class GrayWindowMeans {
 public:
  GrayWindowMeans(const uint8_t* grayLine, const int bytesPerLine, const int width, const int height)
      : m_width(width), m_height(height), m_sums(width, height) {
    for (int y = 0; y < height; ++y) {
      m_sums.pushRow([&](const int x) -> uint32_t { return grayLine[x]; });
      grayLine += bytesPerLine;
    }
  }

  /**
   * Calls formula(x, mean) for every pixel, row by row, and endOfRow() after each row.
   */
  template <typename Formula, typename EndOfRow>
  void forEachPixel(const LocalWindow& window, Formula formula, EndOfRow endOfRow) const {
    local_window_detail::forEachWindow(
        window, m_width, m_height,
        [&](const int x, const QRect& rect, const int area) {
          const double windowSum = m_sums.sum(rect);
          const double rArea = 1.0 / area;
          formula(x, windowSum * rArea);
        },
        endOfRow);
  }

 private:
  int m_width;
  int m_height;
  IntegralImage<uint32_t> m_sums;
};

/**
 * \brief Provides mean and standard deviation of the window around every pixel of a gray image.
 *
 * See the note on the calculation at GrayWindowMeans.
 */
class GrayWindowDeviations {
 public:
  GrayWindowDeviations(const uint8_t* grayLine, const int bytesPerLine, const int width, const int height)
      : m_width(width), m_height(height), m_sums(width, height), m_squareSums(width, height) {
    for (int y = 0; y < height; ++y) {
      m_sums.pushRow([&](const int x) -> uint32_t { return grayLine[x]; });
      m_squareSums.pushRow([&](const int x) -> uint64_t {
        const uint32_t pixel = grayLine[x];
        return pixel * pixel;
      });
      grayLine += bytesPerLine;
    }
  }

  /**
   * Calls formula(x, mean, deviation) for every pixel, row by row, and endOfRow() after each row.
   */
  template <typename Formula, typename EndOfRow>
  void forEachPixel(const LocalWindow& window, Formula formula, EndOfRow endOfRow) const {
    local_window_detail::forEachWindow(
        window, m_width, m_height,
        [&](const int x, const QRect& rect, const int area) {
          const double windowSum = m_sums.sum(rect);
          const double windowSquareSum = m_squareSums.sum(rect);
          const double rArea = 1.0 / area;
          const double mean = windowSum * rArea;
          const double squareMean = windowSquareSum * rArea;
          const double variance = squareMean - mean * mean;
          formula(x, mean, std::sqrt(std::fabs(variance)));
        },
        endOfRow);
  }

 private:
  int m_width;
  int m_height;
  IntegralImage<uint32_t> m_sums;
  IntegralImage<uint64_t> m_squareSums;
};

/** Whether pixel x of a line of a BinaryImage is black. */
inline bool isBlackPixel(const uint32_t* line, const unsigned int x) {
  const uint32_t msb = uint32_t(1) << 31;
  return (line[x >> 5] & (msb >> (x & 31))) != 0;
}

/** Makes pixel x of a line of a BinaryImage black or white. */
inline void setBlackPixel(uint32_t* line, const unsigned int x, const bool black) {
  const uint32_t msb = uint32_t(1) << 31;
  const uint32_t mask = msb >> (x & 31);
  if (black) {
    line[x >> 5] |= mask;
  } else {
    line[x >> 5] &= ~mask;
  }
}
}  // namespace imageproc
#endif  // SCANTAILOR_IMAGEPROC_LOCALWINDOW_H_
