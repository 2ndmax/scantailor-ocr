// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_SPINELINEFINDER_H_
#define SCANTAILOR_PAGE_SPLIT_SPINELINEFINDER_H_

#include <QLineF>
#include <QRectF>
#include <QTransform>
#include <vector>

class DebugImages;

namespace imageproc {
class GrayImage;
}

namespace page_split {
/**
 * \brief Locates the spine of a two-page spread as a near-vertical line that is,
 *        in almost every row of the page, darker than both sides of it.
 *
 * In a row, a column counts if it is the darkest spot within a few pixels and
 * clearly darker than one side and at least a little darker than the other
 * (one page may run into the shadow of the spine). The page height is divided
 * into bands and the weakest band decides. Unlike the spine, the edge of a text
 * column, the frame of a map or the edge of a photo is missing from some bands,
 * usually from the empty margins at the top and bottom. Rows of dark scanner
 * background are not counted, and the drop in brightness is measured from the
 * neighboring pixels, so gray paper works as well as white.
 *
 * The approach follows the spine darkness search of ScanTailor Spectre, which
 * mistook the edges of text columns and photos for the spine on our test scans.
 */
class SpineLineFinder {
 public:
  // Half-width of the search window around the center, as a fraction of the image width.
  static constexpr double CENTER_WINDOW_FRACTION = 0.10;

  // The minimum score of a spine. The lowest score of a real spine on our test scans was 0.32.
  static constexpr double MIN_SCORE = 0.25;

  /**
   * \param grayDownscaled The downscaled (100 dpi) grayscale image, as
   *        produced by VertLineFinder::findLines.
   * \param outToDownscaled Transform mapping virtual output coordinates
   *        to coordinates of \p grayDownscaled. Must be invertible.
   * \param virtualImageRect Bounding rect of the page in virtual output coordinates.
   * \param dbg Optional debug image sink.
   * \param profile If not null, receives the score of each column of \p grayDownscaled
   *        (0 outside the search window).
   * \param score If not null, receives the score of the best line, even if it is too low.
   * \return The detected spine in virtual output coordinates, or a null
   *         QLineF if no line scored high enough.
   */
  static QLineF findSpine(const imageproc::GrayImage& grayDownscaled,
                          const QTransform& outToDownscaled,
                          const QRectF& virtualImageRect,
                          DebugImages* dbg = nullptr,
                          std::vector<double>* profile = nullptr,
                          double* score = nullptr);
};
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_SPINELINEFINDER_H_
