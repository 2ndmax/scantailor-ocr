// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_SKEWDETECTION_H_
#define SCANTAILOR_DESKEW_SKEWDETECTION_H_

#include <QString>

namespace deskew {

/** How the deskew angle is found in automatic mode. */
enum SkewDetection { DETECT_CONTENT, DETECT_TOP_EDGE };

inline QString skewDetectionToString(const SkewDetection detection) {
  return detection == DETECT_TOP_EDGE ? QStringLiteral("top-edge") : QStringLiteral("content");
}

inline SkewDetection skewDetectionFromString(const QString& str, const SkewDetection fallback) {
  if (str == QLatin1String("top-edge")) {
    return DETECT_TOP_EDGE;
  }
  if (str == QLatin1String("content")) {
    return DETECT_CONTENT;
  }
  return fallback;
}
}  // namespace deskew

#endif  // ifndef SCANTAILOR_DESKEW_SKEWDETECTION_H_
