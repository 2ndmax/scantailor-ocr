// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TIFFREADER_H_
#define SCANTAILOR_CORE_TIFFREADER_H_

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class QImage;
class QString;
class ImageMetadata;
class Dpi;

/**
 * \brief Reads TIFF images through libtiff.
 *
 * Diagnostic messages from libtiff are forwarded to ImageLoadErrorCapture.
 */
class TiffReader {
 public:
  static bool canRead(QIODevice& device);

  static ImageMetadataLoader::Status readMetadata(QIODevice& device,
                                                  const VirtualFunction<void, const ImageMetadata&>& out);

  /**
   * \brief Reads the image from io device to QImage.
   *
   * \param device The device to read from.  This device must be
   *        opened for reading and must be seekable.
   * \param pageNum A zero-based page number within a multi-page
   *        TIFF file.
   * \return The resulting image, or a null image in case of failure.
   */
  static QImage readImage(QIODevice& device, int pageNum = 0);

  /**
   * \brief A human readable name of a TIFF compression scheme.
   */
  static QString compressionName(unsigned compression);

  /**
   * \brief Routes libtiff's error and warning messages to ImageLoadErrorCapture.
   *
   * Called automatically when reading.  Code writing TIFF files calls it as well,
   * so write errors can be reported too.  Safe to call any number of times.
   */
  static void installMessageHandlers();

 private:
  class TiffHeader;
  class TiffHandle;

  struct TiffInfo;

  template <typename T>
  class TiffBuffer;

  static TiffHeader readHeader(QIODevice& device);

  static bool checkHeader(const TiffHeader& header);

  static ImageMetadata currentPageMetadata(const TiffHandle& tif);

  static Dpi getDpi(float xres, float yres, unsigned resUnit);

  /**
   * Handles 1 to 8 bit single-channel images (bi-level, grayscale, palette),
   * both striped and tiled, producing Format_Mono or Format_Indexed8.
   */
  static QImage extractBinaryOrIndexed8Image(const TiffHandle& tif, const TiffInfo& info);

  /**
   * Handles floating point, signed integer and 32-bit unsigned integer samples,
   * which libtiff's RGBA interface refuses.  The values are scaled into 8 bits.
   */
  static QImage extractNumericImage(const TiffHandle& tif, const TiffInfo& info);

  /**
   * The general case, based on libtiff's RGBA interface.
   */
  static QImage readRgbaImage(const TiffHandle& tif, const TiffInfo& info);
};


#endif  // ifndef SCANTAILOR_CORE_TIFFREADER_H_
