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
    : QDialog(parent),
      m_pages(pageSelectionAccessor.allPages()),
      m_selectedPages(pageSelectionAccessor.selectedPages()),
      m_curPage(curPage),
      m_scopeGroup(new QButtonGroup(this)),
      m_layoutType(layoutType) {
  setupUi(this);
  m_scopeGroup->addButton(thisPageRB);
  m_scopeGroup->addButton(allPagesRB);
  m_scopeGroup->addButton(thisPageAndFollowersRB);
  m_scopeGroup->addButton(thisEveryOtherRB);
  m_scopeGroup->addButton(everyOtherRB);
  m_scopeGroup->addButton(selectedPagesRB);
  m_scopeGroup->addButton(everyOtherSelectedRB);
  // Pages selected in the thumbnail list are most likely the ones to apply to.
  if (m_selectedPages.size() > 1) {
    selectedPagesRB->setChecked(true);
  } else {
    selectedPagesRB->setEnabled(false);
    selectedPagesHint->setEnabled(false);
    everyOtherSelectedRB->setEnabled(false);
    everyOtherSelectedHint->setEnabled(false);
  }

  // Only a page type set by hand has a split line to apply; an automatic one is detected anew.
  if ((m_layoutType == AUTO_LAYOUT_TYPE) || (m_layoutType == SINGLE_PAGE_UNCUT)) {
    applyCutOption->setEnabled(false);
  }
  connect(applyLayoutTypeOption, &QCheckBox::toggled, this, &SplitModeDialog::updateOptions);
  connect(applyCutOption, &QCheckBox::toggled, this, &SplitModeDialog::updateOptions);
  updateOptions();

  connect(buttonBox, SIGNAL(accepted()), this, SLOT(onSubmit()));
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

  std::set<PageId> pages;

  // thisPageRB is intentionally not handled: the options panel has already applied it.
  if (allPagesRB->isChecked()) {
    m_pages.selectAll().swap(pages);
  } else if (thisPageAndFollowersRB->isChecked()) {
    m_pages.selectPagePlusFollowers(m_curPage).swap(pages);
  } else if (selectedPagesRB->isChecked()) {
    emit accepted(m_selectedPages, layoutType, applyCutOption->isChecked());
    accept();
    return;
  } else if (everyOtherRB->isChecked()) {
    m_pages.selectEveryOther(m_curPage).swap(pages);
  } else if (thisEveryOtherRB->isChecked()) {
    m_pages.selectThisPageAndFollowingEveryOther(m_curPage).swap(pages);
  } else if (everyOtherSelectedRB->isChecked()) {
    m_pages.selectEveryOtherInSubsetFromPage(m_curPage, m_selectedPages).swap(pages);
  }

  emit accepted(pages, layoutType, applyCutOption->isChecked());
  // We assume the default connection from accepted() to accept()
  // was removed.
  accept();
}  // SplitModeDialog::onSubmit
}  // namespace page_split