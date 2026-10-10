// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include "PageSelectionAccessor.h"

namespace fix_orientation {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(onSubmit()));
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  // "This page only" is not handled: the options panel has already applied it.
  if (scopeGroupBox->isAllPages()) {
    emit appliedToAllPages(scopeGroupBox->pages());
  } else if (!scopeGroupBox->isThisPageOnly()) {
    emit appliedTo(scopeGroupBox->pages());
  }

  // We assume the default connection from accept() to accepted() was removed.
  accept();
}  // ApplyDialog::onSubmit
}  // namespace fix_orientation
