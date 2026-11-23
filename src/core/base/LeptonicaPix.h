// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_LEPTONICAPIX_H_
#define SCANTAILOR_CORE_LEPTONICAPIX_H_

#include <memory>

#include <leptonica/allheaders.h>

class QImage;

struct PixDeleter {
  void operator()(PIX* pix) const;
};

using PixPtr = std::unique_ptr<PIX, PixDeleter>;

/**
 * Convert a QImage to a Leptonica PIX (32-bit RGBA). Returns null on failure.
 * Shared by the detectors in core_base so the conversion is not duplicated.
 */
PixPtr qImageToPix(const QImage& qimg);

#endif  // SCANTAILOR_CORE_LEPTONICAPIX_H_