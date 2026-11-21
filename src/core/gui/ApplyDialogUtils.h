// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLYDIALOGUTILS_H_
#define SCANTAILOR_CORE_APPLYDIALOGUTILS_H_

class QCheckBox;
class QDialogButtonBox;

namespace core {
/**
 * Keeps OK disabled while both apply options are unchecked. Such an OK would apply nothing; in
 * Select Content, it would still make the chosen pages be processed again.
 */
void bindOkButtonToApplyOptions(QDialogButtonBox* buttonBox, QCheckBox* firstOption, QCheckBox* secondOption);
}  // namespace core

#endif  // SCANTAILOR_CORE_APPLYDIALOGUTILS_H_
