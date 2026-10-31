// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageSizeLimit.h"

#include <QCoreApplication>
#include <QImageReader>

bool ImageSizeLimit::exceeds(const qint64 width, const qint64 height) {
  if ((width <= 0) || (height <= 0)) {
    return false;
  }
  if ((width > MAX_SIDE) || (height > MAX_SIDE)) {
    return true;
  }
  // Both sides are at most MAX_SIDE here, so the product fits in 64 bit.
  return width * height > MAX_PIXELS;
}

QString ImageSizeLimit::errorMessage(const qint64 width, const qint64 height) {
  return QCoreApplication::translate("ImageSizeLimit",
                                     "The image is too large (%1 × %2 pixels). Width and height may be at most %3 "
                                     "pixels each, the area at most %4 megapixels.")
      .arg(width)
      .arg(height)
      .arg(MAX_SIDE)
      .arg(MAX_PIXELS / 1000000);
}

QString ImageSizeLimit::outputErrorMessage(const QSize& size) {
  // The output size follows from the output resolution and the resolution of the image, which
  // may be wrong (too low).
  return QCoreApplication::translate("ImageSizeLimit",
                                     "The output page would be too large (%1 × %2 pixels). Width and height may be at "
                                     "most %3 pixels each, the area at most %4 megapixels. Lower the output "
                                     "resolution, or check the DPI of the image.")
      .arg(size.width())
      .arg(size.height())
      .arg(MAX_SIDE)
      .arg(MAX_PIXELS / 1000000);
}

OutputTooLargeError::OutputTooLargeError(const QSize& size)
    : std::runtime_error(ImageSizeLimit::outputErrorMessage(size).toStdString()) {}

void ImageSizeLimit::applyQtAllocationLimit() {
  QImageReader::setAllocationLimit(static_cast<int>(MAX_PIXELS * 8 / (1024 * 1024)) + 1);
}
