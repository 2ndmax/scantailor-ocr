// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFIMAGEENCODER_H_
#define SCANTAILOR_CORE_PDFIMAGEENCODER_H_

#include <QByteArray>
#include <QRect>
#include <QSize>

class QImage;
class QString;

/**
 * \brief Encodes images the way PdfWriter embeds them.
 *
 * Errors of the underlying libraries are reported to ImageLoadErrorCapture.
 */
class PdfImageEncoder {
 public:
  /** True for 1 bit images. */
  static bool isBitonal(const QImage& image);

  /**
   * \brief Encodes an image as CCITT G4 (as used by PDF's CCITTFaxDecode with K = -1).
   *
   * The image is converted to 1 bit if necessary.  In the encoded data, dark pixels
   * are black.  Returns an empty array on failure.
   */
  static QByteArray encodeG4(const QImage& image);

  /**
   * \brief Encodes an image losslessly as JBIG2 (as used by PDF's JBIG2Decode).
   *
   * The result is a page information segment and an immediate generic region
   * segment (template 0, arithmetic coding, typical prediction), the embedded
   * stream organisation PDF expects; no global segments are needed.  The image
   * is converted to 1 bit if necessary, dark pixels are black.  Returns an empty
   * array on failure.
   */
  static QByteArray encodeJbig2(const QImage& image);

  /**
   * \brief Encodes an image as baseline JPEG.
   *
   * Gray images are stored with one component, all others as RGB.
   *
   * \param quality 1 to 100.
   * \param[out] components 1 or 3, the number of colour components written.
   * \return The JPEG file, or an empty array on failure.
   */
  static QByteArray encodeJpeg(const QImage& image, int quality, int* components);

  /**
   * \brief The bounding rectangle of all pixels that are not (almost) white.
   *
   * Returns a null rectangle if the whole image is white.
   */
  static QRect contentRect(const QImage& image);

  /**
   * \brief Reads size and bits per pixel of a TIFF file without decoding the image.
   *
   * \return false if the file can't be read as TIFF.
   */
  static bool readTiffInfo(const QString& filePath, QSize* size, int* bitsPerPixel);
};


#endif  // SCANTAILOR_CORE_PDFIMAGEENCODER_H_
