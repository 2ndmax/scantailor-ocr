// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_CHROMADENOISER_H_
#define SCANTAILOR_IMAGEPROC_CHROMADENOISER_H_

class QImage;

namespace imageproc {
/**
 * @brief Removes colour noise: single red, green and blue pixels on gray paper.
 *
 * The image is split into brightness and colour (YCbCr, as in JPEG).  The
 * colour is blurred, so that single coloured pixels take the colour of their
 * surroundings, and colours weaker than a limit become neutral gray.  The
 * brightness is not touched, so text stays sharp.  Strong colours of
 * pictures or coloured text are kept.  Only 32-bit images are changed.
 *
 * @param image The image to process. A null image is allowed.
 * @param radius The radius of the colour blur, in pixels.
 * @param grayLimit Colours weaker than this become gray, fading in up to
 *        twice this value; 0 only blurs.  The colour strength is the distance
 *        from gray in the Cb/Cr plane, 0 to about 180.
 */
void chromaDenoiseInPlace(QImage& image, int radius, int grayLimit);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_CHROMADENOISER_H_
