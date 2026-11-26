// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SavGolFilter.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>

#include "Grayscale.h"
#include "SavGolKernel.h"

namespace imageproc {
namespace {
int64_t calcNumTerms(const int horDegree, const int vertDegree) {
  return static_cast<int64_t>(horDegree + 1) * static_cast<int64_t>(vertDegree + 1);
}

class Kernel : public SavGolKernel {
 public:
  Kernel(const QSize& size, const QPoint& origin, int horDegree, int vertDegree)
      : SavGolKernel(size, origin, horDegree, vertDegree) {}

  void convolve(uint8_t* dst, const uint8_t* srcTopLeft, int srcBpl) const;
};


inline void Kernel::convolve(uint8_t* dst, const uint8_t* srcTopLeft, int srcBpl) const {
  const uint8_t* pSrc = srcTopLeft;
  const float* pKernel = data();
  float sum = 0.5;  // For rounding purposes.
  const int w = width();
  const int h = height();

  for (int y = 0; y < h; ++y, pSrc += srcBpl) {
    for (int x = 0; x < w; ++x) {
      sum += pSrc[x] * *pKernel;
      ++pKernel;
    }
  }

  const auto val = static_cast<int>(sum);
  *dst = static_cast<uint8_t>(qBound(0, val, 255));
}

QImage savGolFilterGrayToGray(const QImage& src, const QSize& windowSize, const int horDegree, const int vertDegree) {
  const int width = src.width();
  const int height = src.height();

  // Kernel width and height.
  const int kw = windowSize.width();
  const int kh = windowSize.height();

  if ((kw > width) || (kh > height)) {
    return src;
  }

  /*
   * Consider a 5x5 kernel:
   * |x|x|T|x|x|
   * |x|x|T|x|x|
   * |L|L|C|R|R|
   * |x|x|B|x|x|
   * |x|x|B|x|x|
   */

  // Co-ordinates of the central point (C) of the kernel.
  const QPoint kCenter(kw / 2, kh / 2);

  // Origin is the current hot spot of the kernel.
  // Normally it's at kCenter, but sometimes we move
  // it to other locations to avoid parts of the kernel
  // from going outside of the image area.
  QPoint kOrigin;

  // Length of the top segment (T) of the kernel.
  const int kTop = kCenter.y();

  // Length of the bottom segment (B) of the kernel.
  const int kBottom = kh - kTop - 1;

  // Length of the left segment (L) of the kernel.
  const int kLeft = kCenter.x();
  // Length of the right segment (R) of the kernel.
  const int kRight = kw - kLeft - 1;

  const uint8_t* const srcData = src.bits();
  const int srcBpl = src.bytesPerLine();

  QImage dst(width, height, QImage::Format_Indexed8);
  dst.setColorTable(createGrayscalePalette());
  if ((width > 0) && (height > 0) && dst.isNull()) {
    throw std::bad_alloc();
  }

  uint8_t* const dstData = dst.bits();
  const int dstBpl = dst.bytesPerLine();
  // Top-left corner.
  const uint8_t* srcLine = srcData;
  uint8_t* dstLine = dstData;
  Kernel kernel(windowSize, QPoint(0, 0), horDegree, vertDegree);
  for (int y = 0; y < kTop; ++y, dstLine += dstBpl) {
    kOrigin.setY(y);
    for (int x = 0; x < kLeft; ++x) {
      kOrigin.setX(x);
      kernel.recalcForOrigin(kOrigin);
      kernel.convolve(dstLine + x, srcLine, srcBpl);
    }
  }

  // Top area between two corners.
  kOrigin.setX(kCenter.x());
  srcLine = srcData;
  dstLine = dstData;
  for (int y = 0; y < kTop; ++y, dstLine += dstBpl) {
    kOrigin.setY(y);
    kernel.recalcForOrigin(kOrigin);
    for (int x = kLeft; x < width - kRight; ++x) {
      kernel.convolve(dstLine + x, srcLine + (x - kLeft), srcBpl);
    }
  }
  // Top-right corner.
  kOrigin.setY(0);
  srcLine = srcData + width - kw;
  dstLine = dstData;
  for (int y = 0; y < kTop; ++y, dstLine += dstBpl) {
    kOrigin.setX(kCenter.x() + 1);
    for (int x = width - kRight; x < width; ++x) {
      kernel.recalcForOrigin(kOrigin);
      kernel.convolve(dstLine + x, srcLine, srcBpl);
      kOrigin.rx() += 1;
    }
    kOrigin.ry() += 1;
  }
  // Central area (Savitzky-Golay filter is separable).
  {
    const SavGolKernel horKernel(QSize(windowSize.width(), 1), QPoint(kCenter.x(), 0), horDegree, 0);
    const SavGolKernel vertKernel(QSize(1, windowSize.height()), QPoint(0, kCenter.y()), 0, vertDegree);

    const int shift = kw - 1;

    // Allocate a 16-byte aligned temporary storage.
    // That may help the compiler to emit efficient SSE code.
    const int tempStride = (width - shift + 3) & ~3;
    AlignedArray<float, 4> tempArray(static_cast<size_t>(tempStride) * static_cast<size_t>(height));
    // Horizontal pass.
    srcLine = srcData;
    float* tempLine = tempArray.data();
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width - shift; ++x) {
        float sum = 0.0f;

        const uint8_t* srcPixels = srcLine + x;
        for (int j = 0; j < kw; ++j) {
          sum += srcPixels[j] * horKernel[j];
        }
        tempLine[x] = sum;
      }
      tempLine += tempStride;
      srcLine += srcBpl;
    }
    // Vertical pass.
    dstLine = dstData + static_cast<ptrdiff_t>(kTop) * dstBpl + kLeft;
    tempLine = tempArray.data();
    for (int y = kTop; y < height - kBottom; ++y) {
      for (int x = 0; x < width - shift; ++x) {
        float sum = 0.5f;  // For rounding purposes, as in Kernel::convolve().

        float* tmp = tempLine + x;
        for (int j = 0; j < kh; ++j, tmp += tempStride) {
          sum += *tmp * vertKernel[j];
        }
        const auto val = static_cast<int>(sum);
        dstLine[x] = static_cast<uint8_t>(qBound(0, val, 255));
      }

      tempLine += tempStride;
      dstLine += dstBpl;
    }
  }

  // Left area between two corners.
  kOrigin.setX(0);
  kOrigin.setY(kCenter.y());
  for (int x = 0; x < kLeft; ++x) {
    srcLine = srcData;
    dstLine = dstData + static_cast<ptrdiff_t>(dstBpl) * kTop;

    kernel.recalcForOrigin(kOrigin);
    for (int y = kTop; y < height - kBottom; ++y) {
      kernel.convolve(dstLine + x, srcLine, srcBpl);
      srcLine += srcBpl;
      dstLine += dstBpl;
    }
    kOrigin.rx() += 1;
  }
  // Right area between two corners.
  kOrigin.setX(kCenter.x() + 1);
  kOrigin.setY(kCenter.y());
  for (int x = width - kRight; x < width; ++x) {
    srcLine = srcData + width - kw;
    dstLine = dstData + static_cast<ptrdiff_t>(dstBpl) * kTop;

    kernel.recalcForOrigin(kOrigin);
    for (int y = kTop; y < height - kBottom; ++y) {
      kernel.convolve(dstLine + x, srcLine, srcBpl);
      srcLine += srcBpl;
      dstLine += dstBpl;
    }
    kOrigin.rx() += 1;
  }

  // Bottom-left corner.
  kOrigin.setY(kCenter.y() + 1);
  srcLine = srcData + static_cast<ptrdiff_t>(srcBpl) * (height - kh);
  dstLine = dstData + static_cast<ptrdiff_t>(dstBpl) * (height - kBottom);
  for (int y = height - kBottom; y < height; ++y, dstLine += dstBpl) {
    for (int x = 0; x < kLeft; ++x) {
      kOrigin.setX(x);
      kernel.recalcForOrigin(kOrigin);
      kernel.convolve(dstLine + x, srcLine, srcBpl);
    }
    kOrigin.ry() += 1;
  }

  // Bottom area between two corners.
  kOrigin.setX(kCenter.x());
  kOrigin.setY(kCenter.y() + 1);
  srcLine = srcData + static_cast<ptrdiff_t>(srcBpl) * (height - kh);
  dstLine = dstData + static_cast<ptrdiff_t>(dstBpl) * (height - kBottom);
  for (int y = height - kBottom; y < height; ++y, dstLine += dstBpl) {
    kernel.recalcForOrigin(kOrigin);
    for (int x = kLeft; x < width - kRight; ++x) {
      kernel.convolve(dstLine + x, srcLine + (x - kLeft), srcBpl);
    }
    kOrigin.ry() += 1;
  }
  // Bottom-right corner.
  kOrigin.setY(kCenter.y() + 1);
  srcLine = srcData + static_cast<ptrdiff_t>(srcBpl) * (height - kh) + (width - kw);
  dstLine = dstData + static_cast<ptrdiff_t>(dstBpl) * (height - kBottom);
  for (int y = height - kBottom; y < height; ++y, dstLine += dstBpl) {
    kOrigin.setX(kCenter.x() + 1);
    for (int x = width - kRight; x < width; ++x) {
      kernel.recalcForOrigin(kOrigin);
      kernel.convolve(dstLine + x, srcLine, srcBpl);
      kOrigin.rx() += 1;
    }
    kOrigin.ry() += 1;
  }
  dst.setDotsPerMeterX(src.dotsPerMeterX());
  dst.setDotsPerMeterY(src.dotsPerMeterY());
  return dst;
}  // savGolFilterGrayToGray
}  // namespace

QImage savGolFilter(const QImage& src, const QSize& windowSize, const int horDegree, const int vertDegree) {
  if ((horDegree < 0) || (vertDegree < 0)) {
    throw std::invalid_argument("savGolFilter: invalid polynomial degree");
  }
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("savGolFilter: invalid window size");
  }

  if (calcNumTerms(horDegree, vertDegree) > static_cast<int64_t>(windowSize.width()) * windowSize.height()) {
    throw std::invalid_argument("savGolFilter: order is too big for that window");
  }
  return savGolFilterGrayToGray(toGrayscale(src), windowSize, horDegree, vertDegree);
}
}  // namespace imageproc
