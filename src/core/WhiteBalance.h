// Copyright (C) 2024  ScanTailor Advanced contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.
// Taken from ScanTailor Spectre (src/core); debug output and unused functions removed.

#ifndef SCANTAILOR_CORE_WHITEBALANCE_H_
#define SCANTAILOR_CORE_WHITEBALANCE_H_

#include <QColor>
#include <QImage>
#include <QRect>

/**
 * \brief White balance correction utilities for removing color casts from scanned pages.
 *
 * This is used to correct yellowed paper, warm lighting, or scanner color casts.
 * The correction is applied before color mode detection to ensure B&W pages
 * aren't misidentified as color due to paper discoloration.
 */
class WhiteBalance {
 public:
  /**
   * \brief Estimate the paper/background color from inside a region of the image.
   *
   * Fallback for pages whose content box hugs the page edges (no usable margins),
   * e.g. aged/toned book paper. Samples the region on a coarse grid, keeps the
   * brightest quartile of sufficiently bright pixels (the paper showing between
   * text), and returns their per-channel median.
   *
   * \param image The source image
   * \param region The area to sample (empty/invalid means the whole image)
   * \return The estimated background color, or invalid QColor if no plausible
   *         paper-like background was found (e.g. a mostly dark photo page)
   */
  static QColor estimateBackgroundColor(const QImage& image, const QRect& region = QRect());

  /**
   * \brief Apply white balance correction to make paperColor neutral.
   *
   * Calculates per-channel multipliers to shift paperColor toward neutral gray,
   * then applies those multipliers to all pixels.
   *
   * \param image The source image (any format; it is converted to RGB32 if it gets corrected)
   * \param paperColor The color that should become neutral
   * \return The corrected image as RGB32, without an alpha channel. If paperColor is invalid, already
   *         neutral or has a zero channel, the image is returned as it is.
   */
  static QImage apply(const QImage& image, const QColor& paperColor);

  /**
   * \brief Check if a color represents a significant cast worth correcting.
   *
   * Returns false if the color is already close to neutral gray,
   * meaning correction would have minimal effect.
   *
   * \param paperColor The detected paper color
   * \param threshold Minimum channel deviation to consider significant (default: 5)
   * \return true if the color has a significant cast
   */
  static bool hasSignificantCast(const QColor& paperColor, int threshold = 5);
};

#endif  // SCANTAILOR_CORE_WHITEBALANCE_H_
