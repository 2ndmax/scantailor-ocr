// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_SHARPEN_H_
#define SCANTAILOR_IMAGEPROC_SHARPEN_H_

class QImage;

namespace imageproc {
class GrayImage;

/**
 * @brief Sharpens edges with an unsharp mask.
 *
 * Each pixel is moved away from the Gaussian blurred image by the given
 * amount: out = in + coef * (in - blurred).
 *
 * @param image The image to process. A null image is allowed.
 * @param radius The radius (sigma) of the blur, in pixels.
 * @param coef The strength; 0 turns it off.
 */
void sharpenInPlace(GrayImage& image, int radius, double coef);

/** @brief The same for colour and grayscale QImages; colours keep their hue. */
void sharpenColorInPlace(QImage& image, int radius, double coef);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_SHARPEN_H_
