// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BlackWhiteBinarization.h"

#include <Binarize.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <RasterOp.h>
#include <imageproc/ExperimentalBinarization.h>

#include <QImage>

#include "BlackWhiteOptions.h"

using namespace imageproc;

namespace output {
BinaryThreshold adjustedThreshold(const BinaryThreshold threshold, const BlackWhiteOptions& options) {
  const int adjusted = threshold + options.thresholdAdjustment();

  // Hard-bounding threshold values is necessary for example
  // if all the content went into the picture mask.
  return BinaryThreshold(qBound(30, adjusted, 225));
}

BinaryImage binarizeBlackWhite(const QImage& image, const BlackWhiteOptions& blackWhiteOptions) {
  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    return BinaryImage(image);
  }

  const BinarizationMethod binarizationMethod = blackWhiteOptions.getBinarizationMethod();

  BinaryImage binarized;
  switch (binarizationMethod) {
    case T_OTSU: {
      GrayscaleHistogram hist(image);
      const BinaryThreshold bwThresh(BinaryThreshold::otsuThreshold(hist));

      binarized = BinaryImage(image, adjustedThreshold(bwThresh, blackWhiteOptions));
      break;
    }
    case T_SAUVOLA: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const double thresholdCoef = blackWhiteOptions.getSauvolaCoef();

      binarized = binarizeSauvola(image, windowsSize, thresholdCoef, thresholdDelta);
      break;
    }
    case T_WOLF: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const auto lowerBound = (unsigned char) blackWhiteOptions.getWolfLowerBound();
      const auto upperBound = (unsigned char) blackWhiteOptions.getWolfUpperBound();
      const double thresholdCoef = blackWhiteOptions.getWolfCoef();

      binarized = binarizeWolf(image, windowsSize, lowerBound, upperBound, thresholdCoef, thresholdDelta);
      break;
    }
    case T_FOX: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const auto lowerBound = (unsigned char) blackWhiteOptions.getWolfLowerBound();
      const auto upperBound = (unsigned char) blackWhiteOptions.getWolfUpperBound();
      const double thresholdCoef = blackWhiteOptions.getWolfCoef();

      binarized = binarizeFox(image, windowsSize, lowerBound, upperBound, thresholdCoef, thresholdDelta);
      break;
    }
    case T_WINDOW: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const auto lowerBound = (unsigned char) blackWhiteOptions.getWolfLowerBound();
      const auto upperBound = (unsigned char) blackWhiteOptions.getWolfUpperBound();
      const double thresholdCoef = blackWhiteOptions.getWolfCoef();

      binarized = binarizeWindow(image, windowsSize, lowerBound, upperBound, thresholdCoef, thresholdDelta);
      break;
    }
    case T_BRADLEY: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const double thresholdCoef = blackWhiteOptions.getSauvolaCoef();

      binarized = binarizeBradley(image, windowsSize, thresholdCoef, thresholdDelta);
      break;
    }
    case T_GRAD: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const auto lowerBound = (unsigned char) blackWhiteOptions.getWolfLowerBound();
      const auto upperBound = (unsigned char) blackWhiteOptions.getWolfUpperBound();
      const double thresholdCoef = blackWhiteOptions.getWolfCoef();

      binarized = binarizeGrad(image, windowsSize, lowerBound, upperBound, thresholdCoef, thresholdDelta);
      break;
    }
    case T_EDGEPLUS: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const double thresholdCoef = blackWhiteOptions.getSauvolaCoef();

      binarized = binarizeEdgeDiv(image, windowsSize, thresholdCoef, 0.0, thresholdDelta);
      break;
    }
    case T_BLURDIV: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const double thresholdCoef = blackWhiteOptions.getSauvolaCoef();

      binarized = binarizeEdgeDiv(image, windowsSize, 0.0, thresholdCoef, thresholdDelta);
      break;
    }
    case T_EDGEDIV: {
      const double thresholdDelta = blackWhiteOptions.thresholdAdjustment();
      const QSize windowsSize = QSize(blackWhiteOptions.getWindowSize(), blackWhiteOptions.getWindowSize());
      const double thresholdCoef = blackWhiteOptions.getSauvolaCoef();

      binarized = binarizeEdgeDiv(image, windowsSize, thresholdCoef, thresholdCoef, thresholdDelta);
      break;
    }
    case T_GATOS:
    case T_NICK:
    case T_SINGH:
    case T_ROBUST:
    case T_MULTISCALE: {
      const GrayImage gray(image);
      const int windowSize = blackWhiteOptions.getWindowSize();
      const double coef = blackWhiteOptions.getSauvolaCoef();
      const int delta = blackWhiteOptions.thresholdAdjustment();
      switch (binarizationMethod) {
        case T_GATOS:
          binarized = binarizeGatos(gray, windowSize, coef, delta);
          break;
        case T_NICK:
          binarized = binarizeNick(gray, windowSize, coef, delta);
          break;
        case T_SINGH:
          binarized = binarizeSingh(gray, windowSize, coef, delta);
          break;
        case T_ROBUST:
          binarized = binarizeRobust(gray, windowSize, coef, delta);
          break;
        default:
          binarized = binarizeMultiScale(gray, windowSize, coef, delta);
          break;
      }
      break;
    }
  }

  // The bounds apply to all methods: darker pixels are always black, lighter ones always white.
  // Wolf, Fox, Window and Grad apply them themselves. For the others the default bounds are left
  // out, so their results stay as they were.
  const int lowerBound = blackWhiteOptions.getWolfLowerBound();
  const int upperBound = blackWhiteOptions.getWolfUpperBound();
  const bool boundsBuiltIn = (binarizationMethod == T_WOLF) || (binarizationMethod == T_FOX)
                             || (binarizationMethod == T_WINDOW) || (binarizationMethod == T_GRAD);
  if (!boundsBuiltIn && ((lowerBound > 1) || (upperBound < 254))) {
    rasterOp<RopAnd<RopSrc, RopDst>>(binarized, BinaryImage(image, BinaryThreshold(upperBound + 1)));
    rasterOp<RopOr<RopSrc, RopDst>>(binarized, BinaryImage(image, BinaryThreshold(lowerBound)));
  }
  return binarized;
}
}  // namespace output
