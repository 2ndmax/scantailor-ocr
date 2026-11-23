// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrientationDetector.h"

#include <QSize>

#include <cmath>

#include <leptonica/allheaders.h>

#include "LeptonicaPix.h"
#include "imageproc/BinaryThreshold.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Scale.h"

OrientationDetector::Result OrientationDetector::decide(double up, double left) {
  Result result;
  result.upConfidence = up;
  result.leftConfidence = left;

  const double minupconf = 8.0;
  const double minratio = 2.5;
  const double absup = std::fabs(up);
  const double absleft = std::fabs(left);

  int orient = L_TEXT_ORIENT_UNKNOWN;
  if (up != 0.0 && left != 0.0) {
    if (up > minupconf && absup > minratio * absleft) {
      orient = L_TEXT_ORIENT_UP;
    } else if (left > minupconf && absleft > minratio * absup) {
      orient = L_TEXT_ORIENT_LEFT;
    } else if (up < -minupconf && absup > minratio * absleft) {
      orient = L_TEXT_ORIENT_DOWN;
    } else if (left < -minupconf && absleft > minratio * absup) {
      orient = L_TEXT_ORIENT_RIGHT;
    }
  }

  switch (orient) {
    case L_TEXT_ORIENT_UP:
      result.decided = true;
      result.angle = 0;
      break;
    case L_TEXT_ORIENT_LEFT:
      result.decided = true;
      result.angle = 90;
      break;
    case L_TEXT_ORIENT_DOWN:
      result.decided = true;
      result.angle = 180;
      break;
    case L_TEXT_ORIENT_RIGHT:
      result.decided = true;
      result.angle = 270;
      break;
    default:
      break;
  }
  return result;
}

OrientationDetector::Result OrientationDetector::detect(const QImage& image, int dpi) {
  Result result;
  if (image.isNull()) {
    return result;
  }

  imageproc::GrayImage gray(image);
  if (dpi > 330) {
    const double f = 300.0 / dpi;
    gray = imageproc::scaleToGray(gray, QSize(qRound(image.width() * f), qRound(image.height() * f)));
  }

  const int threshold = imageproc::BinaryThreshold::otsuThreshold(gray.toQImage());
  const int w = gray.width();
  const int h = gray.height();

  PixPtr pix(pixCreate(w, h, 1));
  if (!pix) {
    return result;
  }
  l_uint32* data = pixGetData(pix.get());
  const int wpl = pixGetWpl(pix.get());
  for (int y = 0; y < h; ++y) {
    const uchar* line = gray.toQImage().constScanLine(y);
    l_uint32* row = data + y * wpl;
    for (int x = 0; x < w; ++x) {
      if (line[x] < threshold) {
        SET_DATA_BIT(row, x);
      }
    }
  }

  l_float32 up = 0.0f, left = 0.0f;
  pixOrientDetect(pix.get(), &up, &left, 0, 0);

  return decide(up, left);
}

OrientationDetector::Result OrientationDetector::detect(const QImage& image, const Dpi& dpi) {
  int d = dpi.horizontal();
  if (d <= 0) {
    d = dpi.vertical();
  }
  if (d <= 0) {
    d = 300;
  }
  return detect(image, d);
}