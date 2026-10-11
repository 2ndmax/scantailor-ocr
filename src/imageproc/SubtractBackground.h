// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_SUBTRACTBACKGROUND_H_
#define SCANTAILOR_IMAGEPROC_SUBTRACTBACKGROUND_H_

class QImage;

namespace imageproc {
class GrayImage;

/**
 * @brief Evens out the paper background: stains, yellowing and shadows.
 *
 * The background is estimated with a median filter of the given radius and
 * subtracted, so that the whole background takes the brightness of its
 * brightest part.  Based on the "subtract background" filter of
 * ScanTailor Experimental.
 *
 * @param image The image to process. A null image is allowed.
 * @param radius The radius of the median window, in pixels.  It should be
 *        clearly larger than the stroke width of the text.
 * @param coef The strength, from 0 (off) to 1 (full).
 */
void subtractBackgroundInPlace(GrayImage& image, int radius, double coef);

/**
 * @brief The same for colour and grayscale QImages.
 *
 * Normally the filter works on the brightness, and colours keep their hue.
 * With @p whitePaper, each colour channel is divided by its own background
 * instead, which takes the paper to white and removes a colour cast such as
 * yellowing; dark text stays dark.
 */
void subtractBackgroundColorInPlace(QImage& image, int radius, double coef, bool whitePaper = false);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_SUBTRACTBACKGROUND_H_
