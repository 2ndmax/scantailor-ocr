// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LeptonicaPix.h"

#include <QImage>

#include <leptonica/allheaders.h>

void PixDeleter::operator()(PIX* pix) const {
  pixDestroy(&pix);
}

PixPtr qImageToPix(const QImage& qimg) {
  QImage img = qimg;

  // Convert to 32-bit ARGB if needed
  if (img.format() != QImage::Format_ARGB32 && img.format() != QImage::Format_RGB32) {
    img = img.convertToFormat(QImage::Format_ARGB32);
  }

  const int w = img.width();
  const int h = img.height();

  PixPtr pix(pixCreate(w, h, 32));
  if (!pix)
    return nullptr;

  l_uint32* pixData = pixGetData(pix.get());
  const int wpl = pixGetWpl(pix.get());

  for (int y = 0; y < h; y++) {
    const QRgb* scanline = reinterpret_cast<const QRgb*>(img.constScanLine(y));
    l_uint32* line = pixData + y * wpl;
    for (int x = 0; x < w; x++) {
      QRgb pixel = scanline[x];
      // Leptonica uses RGBA format
      composeRGBAPixel(qRed(pixel), qGreen(pixel), qBlue(pixel), 255, line + x);
    }
  }

  return pix;
}