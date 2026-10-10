// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyDialog.h"

#include "PageSelectionAccessor.h"

namespace page_layout {
ApplyDialog::ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(onSubmit()));
}

ApplyDialog::~ApplyDialog() = default;

void ApplyDialog::onSubmit() {
  // "This page only" is not handled: the options panel has already applied it.
  emit accepted(scopeGroupBox->isThisPageOnly() ? std::set<PageId>() : scopeGroupBox->pages());

  // We assume the default connection from accepted() to accept()
  // was removed.
  accept();
}  // ApplyDialog::onSubmit
}  // namespace page_layout
