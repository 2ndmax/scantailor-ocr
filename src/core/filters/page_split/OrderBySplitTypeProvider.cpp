// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderBySplitTypeProvider.h"

#include <cassert>
#include <utility>

namespace page_split {
OrderBySplitTypeProvider::OrderBySplitTypeProvider(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)) {}

int OrderBySplitTypeProvider::layoutTypeOf(const Settings& settings, const PageId& page) {
  const Settings::Record record(settings.getPageRecord(page.imageId()));
  const Params* params = record.params();

  int layoutType = record.combinedLayoutType();
  if (params) {
    layoutType = params->pageLayout().toLayoutType();
  }
  if (layoutType == AUTO_LAYOUT_TYPE) {
    layoutType = 100;  // To force it below pages with known layout.
  }
  return layoutType;
}

bool OrderBySplitTypeProvider::compare(const int lhsLayoutType,
                                       const bool lhsIncomplete,
                                       const PageId& lhsPage,
                                       const int rhsLayoutType,
                                       const bool rhsIncomplete,
                                       const PageId& rhsPage) {
  if (lhsIncomplete != rhsIncomplete) {
    // Pages with question mark go to the bottom.
    return rhsIncomplete;
  } else if (lhsIncomplete) {
    assert(rhsIncomplete);
    // Two pages with question marks are ordered naturally.
    return lhsPage < rhsPage;
  }

  assert(!lhsIncomplete);
  assert(!rhsIncomplete);

  if (lhsLayoutType == rhsLayoutType) {
    return lhsPage < rhsPage;
  } else {
    return lhsLayoutType < rhsLayoutType;
  }
}

bool OrderBySplitTypeProvider::precedes(const PageId& lhsPage,
                                        const bool lhsIncomplete,
                                        const PageId& rhsPage,
                                        const bool rhsIncomplete) const {
  // The layout types of pages with question marks don't matter, so they aren't read.
  const int lhsLayoutType = lhsIncomplete ? 0 : layoutTypeOf(*m_settings, lhsPage);
  const int rhsLayoutType = rhsIncomplete ? 0 : layoutTypeOf(*m_settings, rhsPage);
  return compare(lhsLayoutType, lhsIncomplete, lhsPage, rhsLayoutType, rhsIncomplete, rhsPage);
}

PageOrderProvider::Comparator OrderBySplitTypeProvider::snapshot(const std::vector<PageId>& pages) const {
  return snapshotOf<int>(
      pages, [settings = m_settings](const PageId& page) { return layoutTypeOf(*settings, page); }, &compare);
}
}  // namespace page_split