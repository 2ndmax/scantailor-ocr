// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SplitDetectionMethod.h"

namespace page_split {
QString splitDetectionMethodToString(const SplitDetectionMethod method) {
  return (method == SPINE_LINE_DETECTION) ? QStringLiteral("spine-line") : QStringLiteral("edges");
}

SplitDetectionMethod splitDetectionMethodFromString(const QString& method) {
  return (method == QLatin1String("spine-line")) ? SPINE_LINE_DETECTION : EDGES_DETECTION;
}
}  // namespace page_split
