// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTRECTFINDER_H_
#define SCANTAILOR_OUTPUT_OUTPUTRECTFINDER_H_

#include <BinaryImage.h>

#include <QRect>
#include <vector>

namespace output::detail {
/**
 * Finds rectangles of white (content) pixels in mask, for the automatic rectangular picture zones.
 * Below 100 %, sensitivity trims the rectangles to rows and columns mostly of white pixels.
 */
std::vector<QRect> findRectAreas(const imageproc::BinaryImage& mask, int sensitivity);
}  // namespace output::detail

#endif  // SCANTAILOR_OUTPUT_OUTPUTRECTFINDER_H_
