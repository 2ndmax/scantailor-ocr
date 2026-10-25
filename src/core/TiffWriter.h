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

/**
 * How TIFF images are compressed. The values are libtiff's COMPRESSION_* constants.
 *
 * Declared outside TiffWriter, as GCC doesn't accept the member initializers in the default
 * arguments of the enclosing class; there it's TiffWriter::Compression.
 */
struct TiffCompression {
  // The defaults of new profiles and projects.
  /** For black and white images. */
  int bw = COMPRESSION_CCITTFAX4;
  /** For grayscale and color images. */
  int color = COMPRESSION_LZW;
  /** For JPEG compression, MIN_JPEG_QUALITY to MAX_JPEG_QUALITY. */
  int jpegQuality = 85;
  /** The range of jpegQuality, for the settings, the panel and the writer. */
  static constexpr int MIN_JPEG_QUALITY = 10;
  static constexpr int MAX_JPEG_QUALITY = 100;
  /** For palette images, such as posterized pages. */
  int palette = COMPRESSION_LZW;
};

class TiffWriter {
 public:
  using Compression = TiffCompression;

  /**
   * \brief Writes a QImage in TIFF format to a file.
   *
   * Failures are reported to ImageLoadErrorReporter, so the user learns about them.
   * The file is replaced only when the new one is complete, so on failure the previous file
   * stays, and no partially written file is left behind.
   *
   * \param filePath The full path to the file.
   * \param image The image to write. Writing a null image will fail.
   * \param compression The compression; the output step passes the one set by the user
   *        (ProcessingOptions::tiffCompression).
   * \return True on success, false on failure.
   */
  static bool writeImage(const QString& filePath, const QImage& image, const Compression& compression = Compression());

  /**
   * \brief Writes a QImage in TIFF format to an IO device.
   *
   * \param device The device to write to. This device must be
   *        opened for writing and seekable. It stays open; closing it
   *        (and checking it for write errors) is up to the caller.
   * \param image The image to write. Writing a null image will fail.
   * \param compression The compression, see writeImage(const QString&, ...).
   * \return True on success, false on failure.
   *
   * Palette images and images with an alpha channel can't be stored as JPEG; they are
   * stored with LZW instead. Color images are stored as JPEG in YCbCr.
   *
   * Grayscale and color images compressed with LZW, Deflate or LZMA use the horizontal
   * predictor, which makes them smaller. Palette images with up to 16 colors are stored
   * with 4 bits per pixel.
   */
  static bool writeImage(QIODevice& device, const QImage& image, const Compression& compression = Compression());

 private:
  class TiffHandle;

  static bool writeImageToFile(const QString& filePath, const QImage& image, const Compression& compression);

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
