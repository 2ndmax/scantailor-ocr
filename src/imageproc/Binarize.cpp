// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Binarize.h"

#include <QDebug>
#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <vector>

#include "BinaryImage.h"
#include "Grayscale.h"
#include "LocalWindow.h"

namespace imageproc {
namespace {
uint32_t minGrayLevelOf(const QImage& gray) {
  const uint8_t* grayLine = gray.constBits();
  const int grayBpl = gray.bytesPerLine();

  uint8_t minGrayLevel = 255;
  for (int y = 0; y < gray.height(); ++y) {
    minGrayLevel = std::min(minGrayLevel, *std::min_element(grayLine, grayLine + gray.width()));
    grayLine += grayBpl;
  }
  return minGrayLevel;
}
}  // namespace

BinaryImage binarizeOtsu(const QImage& src) {
  return BinaryImage(src, BinaryThreshold::otsuThreshold(src));
}

BinaryImage binarizeMokji(const QImage& src, const unsigned maxEdgeWidth, const unsigned minEdgeMagnitude) {
  const BinaryThreshold threshold(BinaryThreshold::mokjiThreshold(src, maxEdgeWidth, minEdgeMagnitude));
  return BinaryImage(src, threshold);
}

/*
 * sauvola = mean * (1.0 + k * (stderr / 128.0 - 1.0)), k = 0.34
 * modification by zvezdochiot:
 * sauvola = base * (1.0 - k * (1.0 - (frac_s + frac_d))), k = 0.30, delta = 0
 *      base = mean, frac_s = stderr / 128.0, frac_d = delta / 128.0
 */
BinaryImage binarizeSauvola(const QImage& src, const QSize windowSize, const double k, const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeSauvola: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  const GrayWindowDeviations windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  const double frac_d = (double) delta / 128.0;
  windows.forEachPixel(
      [&](const int x, const double mean, const double deviation) {
        const double frac_s = deviation / 128.0;

        const double threshold = mean * (1.0 - k * (1.0 - (frac_s + frac_d)));

        setBlackPixel(bwLine, x, int(grayLine[x]) < threshold);
      },
      [&] {
        grayLine += grayBpl;
        bwLine += bwWpl;
      });
  return bwImg;
}  // binarizeSauvola

/*
 * wolf = mean - k * (mean - min_v) * (1.0 - stderr / stdmax), k = 0.3
 * modification by zvezdochiot:
 * wolf = base * (1.0 - k * (1.0 - (frac_sn + frac_d))) + min_v, k = 0.3, delta = 0
 *      base = mean - min_v, frac_sn = stderr / stdmax, frac_d = delta / 128.0
 */
BinaryImage binarizeWolf(const QImage& src,
                         const QSize windowSize,
                         const unsigned char lowerBound,
                         const unsigned char upperBound,
                         const double k,
                         const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeWolf: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  const uint32_t minGrayLevel = minGrayLevelOf(gray);

  // The multiplication is done in size_t to avoid an int overflow, which
  // would silently allocate buffers that are too small for large images.
  const size_t numPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
  std::vector<float> means(numPixels, 0);
  std::vector<float> deviations(numPixels, 0);

  double maxDeviation = 0;

  {
    // The integral images (12 bytes per pixel, 16 for a window too large for 32-bit sums) are
    // freed at the end of this block, before the binary image is allocated.
    const GrayWindowDeviations windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));

    size_t pixelIndex = 0;
    windows.forEachPixel(
        [&](const int /*x*/, const double mean, const double deviation) {
          maxDeviation = std::max(maxDeviation, deviation);
          means[pixelIndex] = (float) mean;
          deviations[pixelIndex] = (float) deviation;
          ++pixelIndex;
        },
        [] {});
  }
  // A perfectly uniform image (a blank page, for example) has no deviation
  // at all. Without this guard, deviation / maxDeviation below would be
  // 0.0 / 0.0 == NaN, making every threshold comparison false.
  maxDeviation = (maxDeviation > 0.0) ? maxDeviation : 1.0;

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  const double frac_d = (double) delta / 128.0;
  grayLine = gray.bits();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const float mean = means[static_cast<size_t>(y) * w + x];
      const float deviation = deviations[static_cast<size_t>(y) * w + x];
      const double base = mean - minGrayLevel;
      const double frac_sn = deviation / maxDeviation;
      const double threshold = base * (1.0 - k * (1.0 - (frac_sn + frac_d))) + minGrayLevel;

      setBlackPixel(bwLine, x,
                    (grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (int(grayLine[x]) < threshold)));
    }
    grayLine += grayBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeWolf

/*
 * fox= base * (1.0 - k * 0.5 * (1.0 - (frac_sn + frac_d))) +  min_v, k = 0.3, delta = 0
 *      base = mean - min_v, dI = origin - mean, frac_s = dI / (256 - dI), frac_sn = frac_s / max(frac_s), frac_d =
 * delta / 128.0
 */
BinaryImage binarizeFox(const QImage& src,
                        const QSize windowSize,
                        const unsigned char lowerBound,
                        const unsigned char upperBound,
                        const double k,
                        const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeFox: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  const GrayWindowMeans windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));
  const uint32_t minGrayLevel = minGrayLevelOf(gray);

  double maxDeviation = 0.0;

  windows.forEachPixel(
      [&](const int x, const double mean) {
        const double di = (double) grayLine[x] - mean;
        const double deviation = di / (256.0 - di);

        maxDeviation = (deviation > maxDeviation) ? deviation : maxDeviation;
      },
      [&] { grayLine += grayBpl; });
  maxDeviation = (maxDeviation > 0.0) ? maxDeviation : 1.0;

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  const double frac_d = (double) delta / 128.0;
  grayLine = gray.bits();
  windows.forEachPixel(
      [&](const int x, const double mean) {
        const double di = (double) grayLine[x] - mean;
        const double deviation = di / (256.0 - di);

        const double base = mean - minGrayLevel;
        const double frac_sn = deviation / maxDeviation;

        const double threshold = base * (1.0 - k * 0.5 * (1.0 - (frac_sn + frac_d))) + minGrayLevel;

        setBlackPixel(bwLine, x,
                      (grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (int(grayLine[x]) < threshold)));
      },
      [&] {
        grayLine += grayBpl;
        bwLine += bwWpl;
      });
  return bwImg;
}  // binarizeFox

/*
 * window = mean * (1 - k * md / kd), k = 1.0
 * where:
 * md = (mean + 1) / (meanFull + deviation + 1)
 * kd = 1 + kdm * kds
 * kdm = (2 * meanFull + 1) / (deviation + 1)
 * deviationD = deviationMax - deviationMin
 * kds = (deviation - deviationMin) / deviationD if deviationD > 0, 1 if other
 * modification by zvezdochiot:
 * md = (mean + 1 - delta) / (meanFull + deviation + 1), delta = 0
 */
BinaryImage binarizeWindow(const QImage& src,
                           const QSize windowSize,
                           const unsigned char lowerBound,
                           const unsigned char upperBound,
                           const double k,
                           const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeWindow: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  const GrayWindowDeviations windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));

  // The sum of the whole image can exceed 32 bits above about 16.8 million pixels. The sums of the
  // windows are safe: GrayLevelSums uses 64 bits for a window where that could happen.
  uint64_t sumFull = 0;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      sumFull += grayLine[x];
    }
    grayLine += grayBpl;
  }

  const int areaFull = w * h;
  if (areaFull <= 0) {
    return BinaryImage();  // no pixels to average, same result as for a null image
  }
  const uint64_t meanFull = sumFull / areaFull;
  double deviationMax = 0.0;
  double deviationMin = 256.0;
  const double coefw = k * 3.0;  // translate from Wolf to Window coef.

  windows.forEachPixel(
      [&](const int /*x*/, const double /*mean*/, const double deviation) {
        deviationMax = (deviation > deviationMax) ? deviation : deviationMax;
        deviationMin = (deviation < deviationMin) ? deviation : deviationMin;
      },
      [] {});

  const double deviationD = deviationMax - deviationMin;

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  windows.forEachPixel(
      [&](const int x, const double mean, const double deviation) {
        const double md = (mean + 1.0 - delta) / (meanFull + deviation + 1.0);
        const double kdm = (meanFull + meanFull + 1.0) / (deviation + 1.0);
        const double kds = (deviationD > 0.0) ? ((deviation - deviationMin) / deviationD) : 1.0;
        const double kd = 1.0 + kdm * kds;

        const double threshold = mean * (1.0 - coefw * md / kd);

        setBlackPixel(bwLine, x,
                      (grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (int(grayLine[x]) < threshold)));
      },
      [&] {
        grayLine += grayBpl;
        bwLine += bwWpl;
      });
  return bwImg;
}  // binarizeWindow

/*
 * bradlay = mean * (1 - k) + delta, k = 0.34, delta = 0
 */
BinaryImage binarizeBradley(const QImage& src, const QSize windowSize, const double k, const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeBradley: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  const GrayWindowMeans windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  windows.forEachPixel(
      [&](const int x, const double mean) {
        const double threshold = (k < 1.0) ? (mean * (1.0 - k)) : 0;
        setBlackPixel(bwLine, x, int(grayLine[x]) < (threshold + delta));
      },
      [&] {
        grayLine += grayBpl;
        bwLine += bwWpl;
      });
  return bwImg;
}  // binarizeBradley

/*
 * grad = mean * k + meanG * (1.0 - k), meanG = mean(I * G) / mean(G), G = |I - mean|, k = 0.75
 * modification by zvezdochiot:
 * mean = mean + delta, delta = 0
 */
BinaryImage binarizeGrad(const QImage& src,
                         const QSize windowSize,
                         const unsigned char lowerBound,
                         const unsigned char upperBound,
                         const double k,
                         const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeGrad: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  const QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  QImage gmean(toGrayscale(src));
  if (gmean.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  const uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  uint8_t* gmeanLine = gmean.bits();
  const int gmeanBpl = gmean.bytesPerLine();

  const GrayWindowMeans windows(grayLine, grayBpl, w, h, LocalWindow::ofSize(windowSize));

  windows.forEachPixel(
      [&](const int x, const double windowMean) {
        const double mean = windowMean + 0.5 + delta;
        const int imean = (int) ((mean < 0.0) ? 0.0 : (mean < 255.0) ? mean : 255.0);
        gmeanLine[x] = imean;
      },
      [&] { gmeanLine += gmeanBpl; });

  double gvalue = 127.5;
  double sum_g = 0.0, sum_gi = 0.0;
  grayLine = gray.bits();
  gmeanLine = gmean.bits();
  for (int y = 0; y < h; y++) {
    double sum_gl = 0.0;
    double sum_gil = 0.0;
    for (int x = 0; x < w; x++) {
      double gi = grayLine[x];
      double g = gmeanLine[x];
      g -= gi;
      g = (g < 0.0) ? -g : g;
      gi *= g;
      sum_gl += g;
      sum_gil += gi;
    }
    sum_g += sum_gl;
    sum_gi += sum_gil;
    grayLine += grayBpl;
    gmeanLine += gmeanBpl;
  }
  gvalue = (sum_g > 0.0) ? (sum_gi / sum_g) : gvalue;

  double const meanGrad = gvalue * (1.0 - k);

  BinaryImage bwImg(w, h);
  uint32_t* bwLine = bwImg.data();
  const int bwWpl = bwImg.wordsPerLine();

  grayLine = gray.bits();
  gmeanLine = gmean.bits();
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const double origin = grayLine[x];
      const double mean = gmeanLine[x];
      const double threshold = meanGrad + mean * k;
      setBlackPixel(bwLine, x, (grayLine[x] < lowerBound) || ((grayLine[x] <= upperBound) && (origin < threshold)));
    }
    grayLine += grayBpl;
    gmeanLine += gmeanBpl;
    bwLine += bwWpl;
  }
  return bwImg;
}  // binarizeGrad

/*
 * edgediv == EdgeDiv image prefilter before the Otsu threshold
 */
BinaryImage binarizeEdgeDiv(const QImage& src,
                            const QSize windowSize,
                            const double kep,
                            const double kbd,
                            const double delta) {
  if (windowSize.isEmpty()) {
    throw std::invalid_argument("binarizeBlurDiv: invalid windowSize");
  }

  if (src.isNull()) {
    return BinaryImage();
  }

  QImage gray(toGrayscale(src));
  if (gray.isNull()) {
    return BinaryImage();
  }
  const int w = gray.width();
  const int h = gray.height();

  // bits() may make a copy of the pixels with another line length, so it goes first.
  uint8_t* grayLine = gray.bits();
  const int grayBpl = gray.bytesPerLine();

  // The window sums come from the original image; the loop below replaces its pixels one by one.
  const GrayWindowMeans windows(gray.constBits(), grayBpl, w, h, LocalWindow::ofSize(windowSize));

  windows.forEachPixel(
      [&](const int x, const double mean) {
        const double origin = grayLine[x];
        double retval = origin;
        if (kep > 0.0) {
          // EdgePlus
          // edge = I / blur (shift = -0.5) {0.0 .. >1.0}, mean value = 0.5
          const double edge = (retval + 1) / (mean + 1) - 0.5;
          // edgeplus = I * edge, mean value = 0.5 * mean(I)
          const double edgeplus = origin * edge;
          // return k * edgeplus + (1 - k) * I
          retval = kep * edgeplus + (1.0 - kep) * origin;
        }
        if (kbd > 0.0) {
          // BlurDiv
          // edge = blur / I (shift = -0.5) {0.0 .. >1.0}, mean value = 0.5
          const double edgeinv = (mean + 1) / (retval + 1) - 0.5;
          // edgenorm = edge * k + max * (1 - k), mean value = {0.5 .. 1.0} * mean(I)
          const double edgenorm = kbd * edgeinv + (1.0 - kbd);
          // return I / edgenorm
          retval = (edgenorm > 0.0) ? (origin / edgenorm) : origin;
        }
        // trim value {0..255}
        retval = (retval < 0.0) ? 0.0 : (retval < 255.0) ? retval : 255.0;
        grayLine[x] = (int) retval;
      },
      [&] { grayLine += grayBpl; });
  return BinaryImage(gray, (BinaryThreshold::otsuThreshold(gray) + delta));
}  // binarizeBlurDiv

BinaryImage peakThreshold(const QImage& image) {
  return BinaryImage(image, BinaryThreshold::peakThreshold(image));
}
}  // namespace imageproc
