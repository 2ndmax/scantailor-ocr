// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PICTUREDETECTION_H_
#define SCANTAILOR_CORE_PICTUREDETECTION_H_

#include <GrayImage.h>

class TaskStatus;
class DebugImages;

/**
 * Finds picture areas on a page, as the Mixed mode of the Output step does.
 * Takes a grayscale image at about 300 dpi and returns an image of the same size where light
 * areas indicate pictures (threshold 48 separates them).
 * Moved out of OutputGenerator so that the color mode detection can use it too.
 */
imageproc::GrayImage detectPictures(const imageproc::GrayImage& input300dpi,
                                    bool higherSearchSensitivity,
                                    const TaskStatus& status,
                                    DebugImages* dbg = nullptr);

#endif  // SCANTAILOR_CORE_PICTUREDETECTION_H_
