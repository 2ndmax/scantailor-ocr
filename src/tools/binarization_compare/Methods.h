// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_METHODS_H_
#define SCANTAILOR_TOOLS_METHODS_H_

#include <QImage>
#include <QString>
#include <functional>
#include <vector>

#include "GrayImage.h"

namespace binarization_compare {
struct Method {
  // File name of the result, also the key in the viewer.
  QString id;
  // Shown in the viewer.
  QString label;
  // "ours", "experimental" or "filter" (a gray result instead of black and white).
  QString group;
  QString parameters;
  std::function<QImage(const imageproc::GrayImage&)> run;
};

/** All compared methods, ours first. */
const std::vector<Method>& methods();
}  // namespace binarization_compare
#endif  // SCANTAILOR_TOOLS_METHODS_H_
