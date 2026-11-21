// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLYDIALOGUTILS_H_
#define SCANTAILOR_CORE_APPLYDIALOGUTILS_H_

class QCheckBox;
class QDialogButtonBox;

namespace core {
/**
 * Keeps OK disabled while both apply options are unchecked. Applying neither option would
 * silently do nothing.
 */
void bindOkButtonToApplyOptions(QDialogButtonBox* buttonBox, QCheckBox* firstOption, QCheckBox* secondOption);
}  // namespace core

#endif  // SCANTAILOR_CORE_APPLYDIALOGUTILS_H_
