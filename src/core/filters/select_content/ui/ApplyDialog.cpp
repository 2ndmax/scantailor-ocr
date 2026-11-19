// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include "PageSelectionAccessor.h"

namespace select_content {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &ApplyDialog::onSubmit);
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  // "This page only" is not handled: the options panel has already applied it.
  const std::set<PageId> pages = scopeGroupBox->isThisPageOnly() ? std::set<PageId>() : scopeGroupBox->pages();
  emit applySelection(pages, applyContentBoxOption->isChecked(), applyPageBoxOption->isChecked());
  // We assume the default connection from accept() to accepted() was removed.
  accept();
}  // ApplyDialog::onSubmit
}  // namespace select_content
