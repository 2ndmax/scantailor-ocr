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
   * True for palette images that aren't gray, such as posterized pages.
   * Posterized grayscale images can't be told apart from other grayscale images.
   */
  static bool isPalette(const QImage& image);

  /**
   * \brief The uncompressed samples of an image, the way PdfWriter's RAW and FLATE
   *        encodings expect them.
   *
   * 1 bit images become 1 bit gray with black as 0.  Palette images (see isPalette())
   * keep their palette, with 1, 2, 4 or 8 bits per index.  Other gray images become
   * 8 bit gray, all others 8 bit RGB.  Each row starts at a whole byte.
   *
   * \param[out] components 1 or 3.
   * \param[out] bitsPerComponent 1, 2, 4 or 8.
   * \param[out] palette The RGB triples of a palette image, empty otherwise.
   * \return The samples, or an empty array for a null image.
   */
  static QByteArray rawSamples(const QImage& image, int* components, int* bitsPerComponent, QByteArray* palette);

  /**
   * \brief Compresses samples (see rawSamples()) to a zlib stream for PDF's FlateDecode.
   *
   * \param pngPredictors Filter each row with the best fitting PNG predictor first
   *        (PDF's /Predictor 15).  This makes photos much smaller, but doesn't help
   *        with few colours, so it's only meant for 8 bit gray and RGB.
   * \return The stream, or an empty array on failure.
   */
  static QByteArray deflate(const QByteArray& samples,
                            int width,
                            int height,
                            int components,
                            int bitsPerComponent,
                            bool pngPredictors);

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
   * \brief Encodes an image as JPEG 2000 (a JP2 file, as used by PDF's JPXDecode).
   *
   * Gray images are stored with one component, all others as RGB.  To limit the
   * memory used, only a few images are encoded at the same time; further calls wait.
   *
   * \param quality 10 to 100, meant to look about like JPEG with the same value.
   *        100 is lossless.
   * \param[out] components 1 or 3, the number of colour components written.
   * \return The JP2 file, or an empty array on failure.
   */
  static QByteArray encodeJpeg2000(const QImage& image, int quality, int* components);

  /**
   * \brief The bounding rectangle of all pixels that are not (almost) white.
   *
   * Returns a null rectangle if the whole image is white.
   */
  static QRect contentRect(const QImage& image);

  /**
   * \brief Reads size and bits per pixel of a TIFF file without decoding the image.
   *
   * \param[out] palette Optional: whether the image uses a colour palette.
   * \return false if the file can't be read as TIFF.
   */
  static bool readTiffInfo(const QString& filePath, QSize* size, int* bitsPerPixel, bool* palette = nullptr);
};


#endif  // SCANTAILOR_CORE_PDFIMAGEENCODER_H_
