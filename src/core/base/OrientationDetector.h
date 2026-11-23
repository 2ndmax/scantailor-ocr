// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ORIENTATIONDETECTOR_H_
#define SCANTAILOR_CORE_ORIENTATIONDETECTOR_H_

#include "Dpi.h"

#include <QImage>

class OrientationDetector {
 public:
  struct Result {
    bool decided = false;
    int angle = 0;  // clockwise correction, 0/90/180/270
    double upConfidence = 0.0;
    double leftConfidence = 0.0;
  };

  /** Computes the orientation decision from Leptonica's confidence values. */
  static Result decide(double up, double left);

  /** Detects the orientation of a single image. \p dpi is the image resolution. */
  static Result detect(const QImage& image, int dpi);
  static Result detect(const QImage& image, const Dpi& dpi);
};

#endif  // SCANTAILOR_CORE_ORIENTATIONDETECTOR_H_