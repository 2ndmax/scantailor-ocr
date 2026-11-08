// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_
#define SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_

#include "BlackWhiteOptions.h"

class QComboBox;
class QString;

namespace output {
/**
 * Fills a method selector with all binarization methods, in the order of BinarizationMethod,
 * each with a tooltip saying what it is suited for. Used by the Output step and the default
 * parameters dialog.
 */
void fillBinarizationMethodBox(QComboBox* box);

/** The tooltip of the strength field of \p method, naming its default. */
QString binarizationCoefToolTip(BinarizationMethod method);
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_
