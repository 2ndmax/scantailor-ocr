// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include "ApplyDialogUtils.h"
#include "PageSelectionAccessor.h"

namespace fix_orientation {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  core::bindOkButtonToApplyOptions(buttonBox, applyRotationCheckBox, applyTrimCheckBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &ApplyDialog::onSubmit);
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  const bool applyRotation = applyRotationCheckBox->isChecked();
  const bool applyTrim = applyTrimCheckBox->isChecked();

  // "This page only" is not handled: the options panel has already applied it.
  if (!scopeGroupBox->isThisPageOnly()) {
    emit appliedTo(scopeGroupBox->pages(), applyRotation, applyTrim);
  }

  // We assume the default connection from accept() to accepted() was removed.
  accept();
}  // ApplyDialog::onSubmit
}  // namespace fix_orientation
