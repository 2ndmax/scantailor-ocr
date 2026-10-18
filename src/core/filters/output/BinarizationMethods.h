// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_
#define SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_

#include "BlackWhiteOptions.h"

class QComboBox;

namespace output {
/**
 * Fills a method selector with all binarization methods, in the order of BinarizationMethod,
 * each with a tooltip saying what it is suited for. Used by the Output step and the default
 * parameters dialog.
 */
void fillBinarizationMethodBox(QComboBox* box);

/**
 * The methods taken from ScanTailor Experimental need a different window size and strength
 * than the others, which share theirs. When switching to or from one of them, the window size
 * and strength are set to the defaults of the new method.
 */
bool needsOwnBinarizationDefaults(BinarizationMethod from, BinarizationMethod to);

/** Applies the window size and strength defaults of the options' method. */
void applyBinarizationDefaults(BlackWhiteOptions& options);
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_BINARIZATIONMETHODS_H_
