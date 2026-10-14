// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_COLORKMEANS_H_
#define SCANTAILOR_IMAGEPROC_COLORKMEANS_H_

class QImage;

namespace imageproc {
/**
 * @brief Reduces the colors of an image to a few, found with k-means.
 *
 * Idea from ScanTailor Experimental ("K-means").  Meant for colored text after color
 * segmentation, where each letter has one color: the colors are grouped in CIE Lab space, so that
 * distances follow what the eye sees, weighted by their number of pixels, and each pixel gets the
 * average color of its group.  Pure white is the background and stays as it is.  The start is
 * deterministic (the most frequent color, or black, then each time the color farthest from the
 * groups so far), so the same image always gives the same result.
 *
 * @param image A color or grayscale image.
 * @param colors The number of colors besides white, at least 1.
 * @param fixBlack Whether one group is pure black and stays so, as most text is black.
 * @return An RGB32 image.
 */
QImage reduceColorsKMeans(const QImage& image, int colors, bool fixBlack);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_COLORKMEANS_H_
