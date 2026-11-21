// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialogUtils.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>

namespace core {
void bindOkButtonToApplyOptions(QDialogButtonBox* buttonBox, QCheckBox* firstOption, QCheckBox* secondOption) {
  const auto updateOkButton = [=]() {
    if (QPushButton* okButton = buttonBox->button(QDialogButtonBox::Ok)) {
      okButton->setEnabled(firstOption->isChecked() || secondOption->isChecked());
    }
  };
  QObject::connect(firstOption, &QCheckBox::toggled, buttonBox, updateOkButton);
  QObject::connect(secondOption, &QCheckBox::toggled, buttonBox, updateOkButton);
  updateOkButton();
}
}  // namespace core
