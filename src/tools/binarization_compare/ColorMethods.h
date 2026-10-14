// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_COLORMETHODS_H_
#define SCANTAILOR_TOOLS_COLORMETHODS_H_

#include <QImage>
#include <QString>
#include <functional>
#include <vector>

namespace binarization_compare {
/** A way to show colored text in few colors, compared with --colors. */
struct ColorMethod {
  QString id;
  QString label;
  // "ours" (color segmentation of ScanTailor OCR), "experimental" (our K-means) or "filter" (the
  // K-means code of ScanTailor Experimental itself).
  QString group;
  QString parameters;
  // Takes the color page and its black and white text.
  std::function<QImage(const QImage& color, const QImage& text)> run;
};

/** All compared methods, ours first. */
const std::vector<ColorMethod>& colorMethods();

/** The black and white text the methods color: Otsu on the grayscale page, as the Output step by default. */
QImage textOf(const QImage& color);
}  // namespace binarization_compare

#endif  // SCANTAILOR_TOOLS_COLORMETHODS_H_
