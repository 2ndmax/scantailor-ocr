// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include "ApplyDialogUtils.h"
#include "PageSelectionAccessor.h"

namespace deskew {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  core::bindOkButtonToApplyOptions(buttonBox, applyDeskewCheckBox, applyObliqueCheckBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &ApplyDialog::onSubmit);
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  const bool applyDeskew = applyDeskewCheckBox->isChecked();
  const bool applyOblique = applyObliqueCheckBox->isChecked();
  emit appliedTo(scopeGroupBox->pages(), applyDeskew, applyOblique);
  accept();
}  // ApplyDialog::onSubmit
}  // namespace deskew
