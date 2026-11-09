// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_DEWARPINGMETHODBOX_H_
#define SCANTAILOR_OUTPUT_DEWARPINGMETHODBOX_H_

#include <QCoreApplication>

#include "DewarpingOptions.h"

class QComboBox;
class QLabel;

namespace output {
/** The list of dewarping methods, the same in the Output step and in the default parameters. */
class DewarpingMethodBox {
  Q_DECLARE_TR_FUNCTIONS(DewarpingMethodBox)
 public:
  static void setUp(QComboBox* box, QLabel* label);

  /** The method selected in \p box; it is never OFF, which the check box above the list stands for. */
  static DewarpingMode selectedMethod(const QComboBox* box);

  static void selectMethod(QComboBox* box, DewarpingMode method);
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_DEWARPINGMETHODBOX_H_
