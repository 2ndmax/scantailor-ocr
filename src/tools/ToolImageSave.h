// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_
#define SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_

#include <QImage>
#include <QString>
#include <atomic>
#include <iostream>

namespace tools {
namespace detail {
inline std::atomic<int> failedSaves{0};
}

/** Saves the image; if that fails, says so on stderr, so a missing picture of a run doesn't go unnoticed. */
inline bool saveImage(const QImage& image, const QString& path) {
  if (image.save(path)) {
    return true;
  }
  std::cerr << "Cannot write " << path.toStdString() << "\n";
  ++detail::failedSaves;
  return false;
}

/** What a tool returns from main() after its pictures: 1 if any of them is missing, else 0. */
inline int exitCodeOfSaves() {
  return (detail::failedSaves > 0) ? 1 : 0;
}
}  // namespace tools

#endif  // SCANTAILOR_TOOLS_TOOLIMAGESAVE_H_
