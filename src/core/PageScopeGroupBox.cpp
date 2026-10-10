// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageScopeGroupBox.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>

#include "PageSelectionAccessor.h"

namespace {
QLabel* createHint(const QString& text, QWidget* parent) {
  auto* hint = new QLabel(text, parent);
  QFont font(hint->font());
  font.setPointSize(7);
  hint->setFont(font);
  return hint;
}

QHBoxLayout* indented(QWidget* widget) {
  auto* layout = new QHBoxLayout();
  layout->addSpacerItem(new QSpacerItem(30, 13, QSizePolicy::Fixed, QSizePolicy::Minimum));
  layout->addWidget(widget);
  return layout;
}
}  // namespace

PageScopeGroupBox::PageScopeGroupBox(QWidget* parent)
    : QGroupBox(tr("Apply to"), parent), m_group(new QButtonGroup(this)) {
  m_buttons[THIS_PAGE] = new QRadioButton(tr("This page only (already applied)"), this);
  m_buttons[ALL_PAGES] = new QRadioButton(tr("All pages"), this);
  m_buttons[THIS_AND_FOLLOWING] = new QRadioButton(tr("This page and the following ones"), this);
  m_buttons[THIS_AND_EVERY_OTHER_FOLLOWING]
      //: The current page and every second page after it.
      = new QRadioButton(tr("This page and the following every other page"), this);
  //: All odd pages; the text for even pages is used when the current page is even.
  m_buttons[EVERY_OTHER] = new QRadioButton(tr("Every other page (odd pages)"), this);
  m_buttons[SELECTED] = new QRadioButton(tr("Selected pages"), this);
  m_buttons[EVERY_OTHER_SELECTED] = new QRadioButton(tr("Every other selected page"), this);
  m_selectedHint = createHint(tr("Use Ctrl+Click / Shift+Click to select multiple pages."), this);
  m_everyOtherSelectedHint = createHint(tr("The current page will be included."), this);

  auto* layout = new QGridLayout(this);
  int row = 0;
  for (int scope = 0; scope < SCOPE_COUNT; ++scope) {
    m_group->addButton(m_buttons[scope], scope);
    m_counts[scope] = new QLabel(this);
    m_counts[scope]->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_buttons[scope], row, 0);
    layout->addWidget(m_counts[scope], row, 1);
    ++row;
    if (scope == SELECTED) {
      layout->addLayout(indented(m_selectedHint), row++, 0, 1, 2);
    } else if (scope == EVERY_OTHER_SELECTED) {
      layout->addLayout(indented(m_everyOtherSelectedHint), row++, 0, 1, 2);
    }
  }
  layout->setColumnStretch(0, 1);
  m_buttons[THIS_PAGE]->setChecked(true);

  connect(m_group, &QButtonGroup::buttonToggled, this, [this](QAbstractButton*, const bool checked) {
    if (checked) {
      emit scopeChanged();
    }
  });
}

void PageScopeGroupBox::setPages(const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor) {
  m_pages = pageSelectionAccessor.allPages();
  m_curPage = curPage;
  m_selectedPages = pageSelectionAccessor.selectedPages();

  // Page numbers count from 1, so the first page is odd.
  if ((m_pages.pageNo(m_curPage) % 2) != 0) {
    m_buttons[EVERY_OTHER]->setText(tr("Every other page (even pages)"));
  }

  for (int scope = 0; scope < SCOPE_COUNT; ++scope) {
    const auto count = static_cast<int>(pagesFor(static_cast<Scope>(scope)).size());
    m_counts[scope]->setText((count == 1) ? tr("1 page") : tr("%1 pages").arg(count));
  }

  // Pages selected in the thumbnail list are most likely the ones to apply to.
  const bool multipleSelected = m_selectedPages.size() > 1;
  if (multipleSelected) {
    m_buttons[SELECTED]->setChecked(true);
  }
  for (const Scope scope : {SELECTED, EVERY_OTHER_SELECTED}) {
    m_buttons[scope]->setEnabled(multipleSelected);
    m_counts[scope]->setEnabled(multipleSelected);
  }
  m_selectedHint->setEnabled(multipleSelected);
  m_everyOtherSelectedHint->setEnabled(multipleSelected);
}

bool PageScopeGroupBox::isThisPageOnly() const {
  return checkedScope() == THIS_PAGE;
}

bool PageScopeGroupBox::isAllPages() const {
  return checkedScope() == ALL_PAGES;
}

std::set<PageId> PageScopeGroupBox::pages() const {
  return pagesFor(checkedScope());
}

PageScopeGroupBox::Scope PageScopeGroupBox::checkedScope() const {
  const int id = m_group->checkedId();
  return ((id >= 0) && (id < SCOPE_COUNT)) ? static_cast<Scope>(id) : THIS_PAGE;
}

std::set<PageId> PageScopeGroupBox::pagesFor(const Scope scope) const {
  switch (scope) {
    case THIS_PAGE:
      return {m_curPage};
    case ALL_PAGES:
      return m_pages.selectAll();
    case THIS_AND_FOLLOWING:
      return m_pages.selectPagePlusFollowers(m_curPage);
    case THIS_AND_EVERY_OTHER_FOLLOWING:
      return m_pages.selectThisPageAndFollowingEveryOther(m_curPage);
    case EVERY_OTHER:
      return m_pages.selectEveryOther(m_curPage);
    case SELECTED:
      return m_selectedPages;
    case EVERY_OTHER_SELECTED:
      return m_pages.selectEveryOtherInSubset(m_curPage, m_selectedPages);
    case SCOPE_COUNT:
      break;
  }
  return {};
}
