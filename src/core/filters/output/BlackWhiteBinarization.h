// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_BLACKWHITEBINARIZATION_H_
#define SCANTAILOR_OUTPUT_BLACKWHITEBINARIZATION_H_

#include <BinaryImage.h>
#include <BinaryThreshold.h>

class QImage;

namespace output {
class BlackWhiteOptions;

/**
 * The global threshold moved by the threshold adjustment of \p options, kept between 30 and 225.
 */
imageproc::BinaryThreshold adjustedThreshold(imageproc::BinaryThreshold threshold, const BlackWhiteOptions& options);

/**
 * Binarizes \p image with the method and settings of \p options, as the Output step does for
 * black and white content. A binary image is returned as it is.
 */
imageproc::BinaryImage binarizeBlackWhite(const QImage& image, const BlackWhiteOptions& blackWhiteOptions);
}  // namespace output

#endif  // SCANTAILOR_OUTPUT_BLACKWHITEBINARIZATION_H_
