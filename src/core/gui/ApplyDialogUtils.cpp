// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialogUtils.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPointer>
#include <QPushButton>

namespace core {
void bindOkButtonToApplyOptions(QDialogButtonBox* buttonBox, QCheckBox* firstOption, QCheckBox* secondOption) {
  if (!buttonBox || !firstOption || !secondOption) {
    return;
  }
  const QPointer<QDialogButtonBox> safeButtonBox(buttonBox);
  const QPointer<QCheckBox> safeFirst(firstOption);
  const QPointer<QCheckBox> safeSecond(secondOption);
  const auto updateOkButton = [safeButtonBox, safeFirst, safeSecond]() {
    if (!safeButtonBox) {
      return;
    }
    if (QPushButton* okButton = safeButtonBox->button(QDialogButtonBox::Ok)) {
      const bool firstChecked = safeFirst && safeFirst->isChecked();
      const bool secondChecked = safeSecond && safeSecond->isChecked();
      okButton->setEnabled(firstChecked || secondChecked);
    }
  };
  QObject::connect(firstOption, &QCheckBox::toggled, buttonBox, updateOkButton);
  QObject::connect(secondOption, &QCheckBox::toggled, buttonBox, updateOkButton);
  updateOkButton();
}
}  // namespace core
