// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_SPLITDETECTIONMETHOD_H_
#define SCANTAILOR_PAGE_SPLIT_SPLITDETECTIONMETHOD_H_

#include <QString>

namespace page_split {
/**
 * How the split line between two pages is detected automatically.
 */
enum SplitDetectionMethod {
  // The vertical line closest to the center, found by VertLineFinder.
  EDGES_DETECTION,
  // The spine, found by SpineLineFinder, with EDGES_DETECTION as a fallback.
  SPINE_LINE_DETECTION
};

QString splitDetectionMethodToString(SplitDetectionMethod method);

SplitDetectionMethod splitDetectionMethodFromString(const QString& method);
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_SPLITDETECTIONMETHOD_H_
