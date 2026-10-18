// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_KNNDENOISER_H_
#define SCANTAILOR_IMAGEPROC_KNNDENOISER_H_

class QImage;

namespace imageproc {
class GrayImage;

/**
 * @brief Reduces noise while keeping edges sharp ("K nearest neighbours" denoiser).
 *
 * Each pixel is averaged with the means of growing square windows around it,
 * weighted by the window size and by how close the mean is to the pixel, so
 * that windows reaching across an edge count little. Based on the KNN
 * denoiser of ScanTailor Experimental, with symmetric windows.
 *
 * @param image The image to process. A null image is allowed.
 * @param radius The largest window radius, in pixels.
 * @param coef The strength, from 0 (off) to 1.
 */
void knnDenoiseInPlace(GrayImage& image, int radius, double coef);

/** @brief The same for colour and grayscale QImages; colours keep their hue. */
void knnDenoiseColorInPlace(QImage& image, int radius, double coef);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_KNNDENOISER_H_
