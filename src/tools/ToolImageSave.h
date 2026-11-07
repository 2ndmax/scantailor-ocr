// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_
#define SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_

#include <QImage>
#include <QString>
#include <iostream>

namespace tools {
/** Saves the image; if that fails, says so on stderr, so a missing picture of a run doesn't go unnoticed. */
inline bool saveImage(const QImage& image, const QString& path) {
  if (image.save(path)) {
    return true;
  }
  std::cerr << "Cannot write " << path.toStdString() << "\n";
  return false;
}
}  // namespace tools

#endif  // SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_
