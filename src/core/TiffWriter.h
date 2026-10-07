// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TIFFWRITER_H_
#define SCANTAILOR_CORE_TIFFWRITER_H_

#include <tiff.h>

#include <cstddef>
#include <cstdint>

class QIODevice;
class QString;
class QImage;
class Dpm;

class TiffWriter {
 public:
  /** How images are compressed.  The values are libtiff's COMPRESSION_* constants. */
  struct Compression {
    /** For black and white images. */
    int bw;
    /** For grayscale and color images. */
    int color;
    /** For JPEG compression, 1 to 100. */
    int jpegQuality;
    /** For palette images, such as posterized pages. */
    int palette = COMPRESSION_LZW;

    /** The compression set by the user. */
    static Compression fromSettings();
  };

  /**
   * \brief Writes a QImage in TIFF format to a file.
   *
   * Failures are reported to ImageLoadErrorReporter, so the user learns about them.
   * No partially written file is left behind.
   *
   * \param filePath The full path to the file.
   * \param image The image to write.  Writing a null image will fail.
   * \return True on success, false on failure.
   */
  static bool writeImage(const QString& filePath, const QImage& image);

  /**
   * \brief Writes a QImage in TIFF format to an IO device.
   *
   * \param device The device to write to.  This device must be
   *        opened for writing and seekable.
   * \param image The image to write.  Writing a null image will fail.
   * \return True on success, false on failure.
   */
  static bool writeImage(QIODevice& device, const QImage& image);

  /**
   * \brief Like writeImage(QIODevice&, const QImage&), but with the given compression
   *        instead of the one set by the user.
   *
   * Palette images and images with an alpha channel can't be stored as JPEG; they are
   * stored with LZW instead.  Color images are stored as JPEG in YCbCr.
   *
   * Grayscale and color images compressed with LZW, Deflate or LZMA use the horizontal
   * predictor, which makes them smaller.  Palette images with up to 16 colors are stored
   * with 4 bits per pixel.
   */
  static bool writeImage(QIODevice& device, const QImage& image, const Compression& compression);

 private:
  class TiffHandle;

  static bool writeImageToFile(const QString& filePath, const QImage& image);

  static void setDpm(const TiffHandle& tif, const Dpm& dpm);

  static void setCompression(const TiffHandle& tif, int compression, int jpegQuality);

  /** For 8-bit samples: sets the horizontal predictor if the compression benefits from it. */
  static void setPredictor(const TiffHandle& tif, int compression);

  static bool writeBitonalOrIndexed8Image(const TiffHandle& tif, const QImage& image, const Compression& compression);

  static bool writeRGB32Image(const TiffHandle& tif, const QImage& image, const Compression& compression);

  static bool writeARGB32Image(const TiffHandle& tif, const QImage& image, const Compression& compression);

  static bool write8bitLines(const TiffHandle& tif, const QImage& image);

  static bool write4bitLines(const TiffHandle& tif, const QImage& image);

  static bool writeBinaryLinesAsIs(const TiffHandle& tif, const QImage& image);

  static bool writeBinaryLinesReversed(const TiffHandle& tif, const QImage& image);

  static const uint8_t m_reverseBitsLUT[256];
};


#endif  // ifndef SCANTAILOR_CORE_TIFFWRITER_H_
