// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFCOMPRESSION_H_
#define SCANTAILOR_CORE_PDFCOMPRESSION_H_

/**
 * \brief How the PDF export compresses images.
 *
 * The values are stored in the application settings, so they must not change.
 */
enum class PdfCompression {
  NONE = 0,
  /** Lossless. */
  DEFLATE = 1,
  JPEG = 2,
  JPEG2000 = 3,
  /** Lossless, 1 bit images only. */
  CCITT_G4 = 4,
  /** Lossless, 1 bit images only. */
  JBIG2 = 5
};

/**
 * Whether a method loses image quality. JPEG always does, even at quality 100; JPEG 2000 is
 * lossless at quality 100.
 */
inline bool isLossyPdfCompression(const PdfCompression compression, const int quality) {
  return (compression == PdfCompression::JPEG) || ((compression == PdfCompression::JPEG2000) && (quality < 100));
}

#endif  // SCANTAILOR_CORE_PDFCOMPRESSION_H_
