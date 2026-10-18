// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_BRIGHTNESSFILTER_H_
#define SCANTAILOR_IMAGEPROC_BRIGHTNESSFILTER_H_

#include <functional>

class QImage;

namespace imageproc {
class GrayImage;

/**
 * @brief Applies a filter working on gray levels to a colour or grayscale image.
 *
 * The filter gets the brightness of the image. In 8-bit images the result
 * replaces the gray levels; in 32-bit images the change of brightness is
 * carried over to the colour channels, so that colours keep their hue.
 * Other formats are left alone.
 */
void filterBrightnessInPlace(QImage& image, const std::function<void(GrayImage&)>& filter);
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_BRIGHTNESSFILTER_H_
