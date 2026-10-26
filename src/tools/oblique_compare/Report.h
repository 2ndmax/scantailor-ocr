// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_OBLIQUE_COMPARE_REPORT_H_
#define SCANTAILOR_TOOLS_OBLIQUE_COMPARE_REPORT_H_

#include <QPointF>
#include <QString>
#include <optional>
#include <vector>

#include "TestPage.h"

namespace compare {

/** What one oblique method found on one page. Nothing means no angle; the program then keeps 0°. */
struct MethodResult {
  std::optional<double> shear;
  /** On the same scan moved by a fraction of a pixel (RunInfo::shift). */
  std::optional<double> shearShifted;
  /** On the scan, as far as the method measures it; in the method's own scale. */
  std::optional<double> confidence;
  /** The time the method took on the scan. */
  double milliseconds = 0;
};

struct PageResult {
  PageSpec spec;
  /** The deskew angle found first; the oblique angle is measured on the page turned by it. */
  std::optional<double> rotation;
  /** In the order of RunInfo::methods. */
  std::vector<MethodResult> methods;

  /** The shear left on the page after the program applies what \p method found. */
  double residualShear(const int method) const { return methods[method].shear.value_or(0.0) - spec.shearDeg; }
};

struct CaseImage {
  /** Index into RunInfo::methods. */
  int method = 0;
  /** Relative to the output folder. */
  QString file;
  QString caption;
};

struct RunInfo {
  int pages = 0;
  uint32_t seed = 0;
  QString fontFamily;
  double seconds = 0;
  QPointF shift;
  std::vector<QString> methods;
};

/** Writes bericht.html, ergebnisse.csv and zusammenfassung.txt into \p outDir. */
bool writeReport(const QString& outDir,
                 const std::vector<PageResult>& results,
                 const std::vector<CaseImage>& cases,
                 const RunInfo& info,
                 QString* error);

}  // namespace compare

#endif
