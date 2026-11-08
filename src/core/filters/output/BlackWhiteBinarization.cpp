// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BlackWhiteBinarization.h"

#include <Binarize.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <RasterOp.h>
#include <imageproc/ExperimentalBinarization.h>

#include <QImage>

#include "BinarizationMethods.h"
#include "BlackWhiteOptions.h"

using namespace imageproc;

namespace output {
BinaryThreshold adjustedThreshold(const BinaryThreshold threshold, const BlackWhiteOptions& options) {
  const int adjusted = threshold + options.values().thresholdAdjustment;

  // Hard-bounding threshold values is necessary for example
  // if all the content went into the picture mask.
  return BinaryThreshold(qBound(30, adjusted, 225));
}

BinaryImage binarizeBlackWhite(const QImage& image, const BlackWhiteOptions& blackWhiteOptions) {
  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    return BinaryImage(image);
  }

  const BinarizationMethod binarizationMethod = blackWhiteOptions.getBinarizationMethod();

  // Otsu has no strength or window of its own and ignores these.
  const BlackWhiteOptions::MethodValues values = blackWhiteOptions.values();
  const double thresholdCoef = values.coef;
  const int thresholdDelta = values.thresholdAdjustment;
  const int windowLength = values.windowSize;
  const QSize windowSize(windowLength, windowLength);
  const int lowerBound = values.lowerBound;
  const int upperBound = values.upperBound;
  const auto lowerByte = static_cast<unsigned char>(lowerBound);
  const auto upperByte = static_cast<unsigned char>(upperBound);

  BinaryImage binarized;
  switch (binarizationMethod) {
    case T_OTSU: {
      GrayscaleHistogram hist(image);
      const BinaryThreshold bwThresh(BinaryThreshold::otsuThreshold(hist));

      binarized = BinaryImage(image, adjustedThreshold(bwThresh, blackWhiteOptions));
      break;
    }
    case T_SAUVOLA:
      binarized = binarizeSauvola(image, windowSize, thresholdCoef, thresholdDelta);
      break;
    case T_WOLF:
      binarized = binarizeWolf(image, windowSize, lowerByte, upperByte, thresholdCoef, thresholdDelta);
      break;
    case T_FOX:
      binarized = binarizeFox(image, windowSize, lowerByte, upperByte, thresholdCoef, thresholdDelta);
      break;
    case T_WINDOW:
      binarized = binarizeWindow(image, windowSize, lowerByte, upperByte, thresholdCoef, thresholdDelta);
      break;
    case T_BRADLEY:
      binarized = binarizeBradley(image, windowSize, thresholdCoef, thresholdDelta);
      break;
    case T_GRAD:
      binarized = binarizeGrad(image, windowSize, lowerByte, upperByte, thresholdCoef, thresholdDelta);
      break;
    case T_EDGEPLUS:
      binarized = binarizeEdgeDiv(image, windowSize, thresholdCoef, 0.0, thresholdDelta);
      break;
    case T_BLURDIV:
      binarized = binarizeEdgeDiv(image, windowSize, 0.0, thresholdCoef, thresholdDelta);
      break;
    case T_EDGEDIV:
      binarized = binarizeEdgeDiv(image, windowSize, thresholdCoef, thresholdCoef, thresholdDelta);
      break;
    case T_GATOS:
      binarized = binarizeGatos(GrayImage(image), windowLength, thresholdCoef, thresholdDelta);
      break;
    case T_NICK:
      binarized = binarizeNick(GrayImage(image), windowLength, thresholdCoef, thresholdDelta);
      break;
    case T_SINGH:
      binarized = binarizeSingh(GrayImage(image), windowLength, thresholdCoef, thresholdDelta);
      break;
    case T_ROBUST:
      binarized = binarizeRobust(GrayImage(image), windowLength, thresholdCoef, thresholdDelta);
      break;
    case T_MULTISCALE:
      binarized = binarizeMultiScale(GrayImage(image), windowLength, thresholdCoef, thresholdDelta);
      break;
  }

  // The bounds apply to all methods: darker pixels are always black, lighter ones always white.
  // Wolf, Fox, Window and Grad apply them themselves. For the others the default bounds are left
  // out, so their results stay as they were.
  const bool boundsBuiltIn = (binarizationMethod == T_WOLF) || (binarizationMethod == T_FOX)
                             || (binarizationMethod == T_WINDOW) || (binarizationMethod == T_GRAD);
  if (!boundsBuiltIn && ((lowerBound > 1) || (upperBound < 254))) {
    rasterOp<RopAnd<RopSrc, RopDst>>(binarized, BinaryImage(image, BinaryThreshold(upperBound + 1)));
    rasterOp<RopOr<RopSrc, RopDst>>(binarized, BinaryImage(image, BinaryThreshold(lowerBound)));
  }
  return binarized;
}
}  // namespace output
