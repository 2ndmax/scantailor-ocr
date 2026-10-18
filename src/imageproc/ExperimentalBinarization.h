// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_EXPERIMENTALBINARIZATION_H_
#define SCANTAILOR_IMAGEPROC_EXPERIMENTALBINARIZATION_H_

namespace imageproc {
class BinaryImage;
class GrayImage;

// Binarization methods taken over from ScanTailor Experimental. The window size is the one of
// the options panel (about twice the radius used in Experimental), k the strength and delta the
// threshold adjustment, where a positive value makes the result darker.

/**
 * Gatos, Pratikakis and Perantonis (2006): estimates the paper background from a Niblack
 * binarization and thresholds by the distance to it. Suited to yellowed, stained or unevenly
 * lit pages.
 */
BinaryImage binarizeGatos(const GrayImage& src, int windowSize = 200, double k = 0.2, int delta = 0);

/**
 * Khurshid, Siddiqi, Faure and Vincent (2009): a variant of Niblack with less noise on empty
 * paper, for faint print on old documents.
 */
BinaryImage binarizeNick(const GrayImage& src, int windowSize = 200, double k = 0.1, int delta = 0);

/**
 * Singh, Sinam, James and Singh (2012): a local threshold from the mean and the local contrast,
 * computed quickly.
 */
BinaryImage binarizeSingh(const GrayImage& src, int windowSize = 200, double k = 0.3, int delta = 0);

/**
 * Liao (2017): "OFF center-surround cells", a dark centre against a brighter surround, hardly
 * affected by shading.
 */
BinaryImage binarizeRobust(const GrayImage& src, int windowSize = 40, double k = 0.2, int delta = 0);

/**
 * Combines thresholds over several window sizes, for pages with text of very different sizes.
 */
BinaryImage binarizeMultiScale(const GrayImage& src, int windowSize = 40, double k = 0.2, int delta = 0);
}  // namespace imageproc
#endif  // SCANTAILOR_IMAGEPROC_EXPERIMENTALBINARIZATION_H_
