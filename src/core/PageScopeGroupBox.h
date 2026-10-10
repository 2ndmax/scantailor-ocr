// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PAGESCOPEGROUPBOX_H_
#define SCANTAILOR_CORE_PAGESCOPEGROUPBOX_H_

#include <QGroupBox>
#include <array>
#include <set>

#include "PageId.h"
#include "PageSequence.h"

class PageSelectionAccessor;
class QButtonGroup;
class QLabel;
class QRadioButton;

/**
 * \brief The "Apply to" part of the "Apply to ..." windows: which pages to apply to.
 *
 * All windows offer the same seven choices.  Each shows how many pages it applies to, and
 * "Every other page" names the odd or even pages.  When several pages are selected in the
 * thumbnail list, "Selected pages" is preselected.
 */
class PageScopeGroupBox : public QGroupBox {
  Q_OBJECT
 public:
  explicit PageScopeGroupBox(QWidget* parent = nullptr);

  /** Takes the pages to choose from; call it once before the window is shown. */
  void setPages(const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor);

  /** "This page only (already applied)" is chosen. */
  bool isThisPageOnly() const;

  /** "All pages" is chosen. */
  bool isAllPages() const;

  /** The pages of the chosen entry; for "This page only" the current page. */
  std::set<PageId> pages() const;

 signals:
  void scopeChanged();

 private:
  enum Scope {
    THIS_PAGE,
    ALL_PAGES,
    THIS_AND_FOLLOWING,
    THIS_AND_EVERY_OTHER_FOLLOWING,
    EVERY_OTHER,
    SELECTED,
    EVERY_OTHER_SELECTED,
    SCOPE_COUNT
  };

  Scope checkedScope() const;

  std::set<PageId> pagesFor(Scope scope) const;

  PageSequence m_pages;
  PageId m_curPage;
  std::set<PageId> m_selectedPages;
  QButtonGroup* m_group;
  std::array<QRadioButton*, SCOPE_COUNT> m_buttons{};
  std::array<QLabel*, SCOPE_COUNT> m_counts{};
  QLabel* m_selectedHint;
  QLabel* m_everyOtherSelectedHint;
};

#endif  // SCANTAILOR_CORE_PAGESCOPEGROUPBOX_H_
