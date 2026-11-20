// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTMORPHOLOGYOPS_H_
#define SCANTAILOR_OUTPUT_OUTPUTMORPHOLOGYOPS_H_

#include <BinaryImage.h>

namespace output::detail {
void hitMissReplaceAllDirections(imageproc::BinaryImage& img,
                                 const char* const pattern,
                                 int patternWidth,
                                 int patternHeight);
}  // namespace output::detail

#endif  // SCANTAILOR_OUTPUT_OUTPUTMORPHOLOGYOPS_H_
