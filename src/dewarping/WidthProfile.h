// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DEWARPING_WIDTHPROFILE_H_
#define SCANTAILOR_DEWARPING_WIDTHPROFILE_H_

#include <QRect>
#include <vector>

#include "WidthCorrection.h"

namespace imageproc {
class GrayImage;
}

namespace dewarping {
/**
 * \brief Measures how wide the letters came out across a dewarped page.
 *
 * The vertical stems of letters have the same width everywhere on a printed
 * page. Where the dewarped text is compressed horizontally, they get
 * narrower; horizontal strokes keep their thickness. So the ratio of short
 * horizontal dark runs (stem widths) to short vertical dark runs (stroke
 * thicknesses) shows the local horizontal scale. It is measured in windows
 * of about 7 mm sliding across the text.
 */
class WidthProfile {
 public:
  struct Sample {
    double x;      // Window centre, in image coordinates.
    double scale;  // Relative horizontal scale; 1 is the median of the page.
  };

  /**
   * \param image The dewarped page.
   * \param area The part of the image to measure, normally the content box.
   * \param dpi The horizontal resolution of the image.
   * \return The measured windows, from left to right. Empty when the page
   *         carries too little text.
   */
  static std::vector<Sample> measure(const imageproc::GrayImage& image, const QRect& area, int dpi);

  /**
   * \brief Turns a measured profile into a width correction.
   *
   * \param samples As returned by measure().
   * \param modelLeft, modelWidth Where the dewarping model domain lies in the
   *        image, so that image x maps to the normalized position
   *        (x - modelLeft) / modelWidth.
   * \return A null correction if the profile is not reliable or the
   *         deviations are too small to be worth correcting.
   */
  static WidthCorrection toCorrection(const std::vector<Sample>& samples, double modelLeft, double modelWidth);
};
}  // namespace dewarping

#endif  // SCANTAILOR_DEWARPING_WIDTHPROFILE_H_
