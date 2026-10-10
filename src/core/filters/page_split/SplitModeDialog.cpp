// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SplitModeDialog.h"

#include <QPushButton>

#include "PageSelectionAccessor.h"

namespace page_split {
SplitModeDialog::SplitModeDialog(QWidget* const parent,
                                 const PageId& curPage,
                                 const PageSelectionAccessor& pageSelectionAccessor,
                                 const LayoutType layoutType)
    : QDialog(parent), m_layoutType(layoutType) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  // Only a page type set by hand has a split line to apply; an automatic one is detected anew.
  if ((m_layoutType == AUTO_LAYOUT_TYPE) || (m_layoutType == SINGLE_PAGE_UNCUT)) {
    applyCutOption->setEnabled(false);
  }
  connect(applyLayoutTypeOption, &QCheckBox::toggled, this, &SplitModeDialog::updateOptions);
  connect(applyCutOption, &QCheckBox::toggled, this, &SplitModeDialog::updateOptions);
  updateOptions();

  connect(buttonBox, &QDialogButtonBox::accepted, this, &SplitModeDialog::onSubmit);
}

SplitModeDialog::~SplitModeDialog() = default;

void SplitModeDialog::updateOptions() {
  // The split line can only be applied together with the page type.
  if (applyCutOption->isChecked()) {
    applyLayoutTypeOption->setChecked(true);
  }
  applyLayoutTypeOption->setEnabled(!applyCutOption->isChecked());
  // Applying nothing would silently do nothing.
  if (QPushButton* okButton = buttonBox->button(QDialogButtonBox::Ok)) {
    okButton->setEnabled(applyLayoutTypeOption->isChecked());
  }
}

void SplitModeDialog::onSubmit() {
  const LayoutType layoutType = m_layoutType;

  // "This page only" is not handled: the options panel has already applied it.
  const std::set<PageId> pages = scopeGroupBox->isThisPageOnly() ? std::set<PageId>() : scopeGroupBox->pages();

  emit accepted(pages, layoutType, applyCutOption->isChecked());
  // We assume the default connection from accepted() to accept()
  // was removed.
  accept();
}  // SplitModeDialog::onSubmit
}  // namespace page_split