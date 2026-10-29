// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGESIZELIMIT_H_
#define SCANTAILOR_CORE_IMAGESIZELIMIT_H_

#include <QSize>
#include <QString>
#include <QtGlobal>

/**
 * \brief The largest image the program processes, the same for every place that checks it.
 *
 * Larger images are refused with errorMessage() instead of being decoded or created: a small,
 * damaged file may claim a size whose pixels don't fit in memory, and the processing computes
 * some byte offsets and pixel counts in int.
 */
class ImageSizeLimit {
 public:
  /**
   * The longest side. Tesseract refuses images with a longer side, and the despeckling stores
   * distances in 16 bit.
   */
  static constexpr int MAX_SIDE = 32767;

  /**
   * The largest number of pixels: A0 at 600 DPI (558 megapixels) fits. Pixel counts in int
   * overflow at 2^31.
   */
  static constexpr qint64 MAX_PIXELS = 600000000;

  /**
   * Whether an image of \p width x \p height pixels is too large. Sizes are taken as 64 bit, as
   * image headers may claim more than fits in int. Empty or negative sizes don't exceed the limit.
   */
  static bool exceeds(qint64 width, qint64 height);

  static bool exceeds(const QSize& size) { return exceeds(size.width(), size.height()); }

  /** The message for the user about an image of \p width x \p height pixels that is too large. */
  static QString errorMessage(qint64 width, qint64 height);

  static QString errorMessage(const QSize& size) { return errorMessage(size.width(), size.height()); }
};


#endif  // ifndef SCANTAILOR_CORE_IMAGESIZELIMIT_H_
