// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_JP2READER_H_
#define SCANTAILOR_CORE_JP2READER_H_

#include <QSize>
#include <cstdint>

#include "ImageMetadataLoader.h"
#include "VirtualFunction.h"

class QIODevice;
class QImage;
class ImageMetadata;

/**
 * \brief Reads JPEG 2000 images (JP2 / JPX / JPH files and raw J2K / J2C codestreams)
 *        through OpenJPEG.
 *
 * Diagnostic messages from OpenJPEG are forwarded to ImageLoadErrorCapture.
 */
class Jp2Reader {
 public:
  static bool canRead(QIODevice& device);

  /**
   * \brief Reads the image size and resolution.
   *
   * Only the file header is parsed, the image data isn't touched, which makes
   * this very fast even for huge files.
   */
  static ImageMetadataLoader::Status readMetadata(QIODevice& device,
                                                  const VirtualFunction<void, const ImageMetadata&>& out);

  /**
   * \brief Decodes the image.
   *
   * \param device The device to read from.  Must be opened for reading and seekable.
   * \param minSize If valid, the image may be decoded at a lower resolution level
   *        (a power of two smaller), as long as the result still is at least
   *        this large in both dimensions.  JPEG 2000 can do that without decoding
   *        the full resolution, which makes generating thumbnails very fast.
   * \return The resulting image (Format_Indexed8 for grayscale, Format_RGB32 or
   *         Format_ARGB32 for color), or a null image in case of failure.
   */
  static QImage readImage(QIODevice& device, const QSize& minSize = QSize());

  /**
   * \brief Changes when huge images are decoded strip by strip.
   *
   * Only meant for tests, which can't easily use images large enough to reach
   * the defaults (1 GiB of decoded samples, 128 MiB per strip, at least 256 rows).
   */
  static void setStripDecodingParameters(uint64_t thresholdBytes, uint64_t stripBytes, uint32_t minStripRows);
};


#endif  // ifndef SCANTAILOR_CORE_JP2READER_H_
