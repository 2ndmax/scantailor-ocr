// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include <QPushButton>

#include "PageSelectionAccessor.h"

namespace deskew {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  // Applying neither the deskew nor the oblique angle would silently do nothing.
  const auto updateOkButton = [this]() {
    if (QPushButton* okButton = buttonBox->button(QDialogButtonBox::Ok)) {
      okButton->setEnabled(applyDeskewCheckBox->isChecked() || applyObliqueCheckBox->isChecked());
    }
  };
  connect(applyDeskewCheckBox, &QCheckBox::toggled, this, updateOkButton);
  connect(applyObliqueCheckBox, &QCheckBox::toggled, this, updateOkButton);
  updateOkButton();

  connect(buttonBox, &QDialogButtonBox::accepted, this, &ApplyDialog::onSubmit);
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  const bool applyDeskew = applyDeskewCheckBox->isChecked();
  const bool applyOblique = applyObliqueCheckBox->isChecked();

  const std::set<PageId> pages = scopeGroupBox->pages();
  if (scopeGroupBox->isAllPages()) {
    emit appliedToAllPages(pages, applyDeskew, applyOblique);
  } else {
    emit appliedTo(pages, applyDeskew, applyOblique);
  }
  accept();
}  // ApplyDialog::onSubmit
}  // namespace deskew