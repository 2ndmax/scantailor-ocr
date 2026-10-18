// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_COLOREDDIMMING_H_
#define SCANTAILOR_IMAGEPROC_COLOREDDIMMING_H_

class QImage;

namespace imageproc {
/**
 * @brief Darkens or lightens colored pixels, before binarization.
 *
 * Idea from ScanTailor Experimental ("dimming colored"), extended to lightening. The amount
 * depends on the saturation of a pixel (max - min of red, green and blue): gray pixels stay as
 * they are. With a positive coef a pixel moves towards black by coef * saturation / 255, at most
 * all the way; with a negative coef it moves towards white in the same way.
 *
 * @param image A color or grayscale image.
 * @param coef -1 to 2; 0 changes nothing.
 * @return The image itself if nothing changes (a grayscale image or coef 0), else an RGB32 copy.
 */
QImage dimColoredPixels(const QImage& image, double coef);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_COLOREDDIMMING_H_
