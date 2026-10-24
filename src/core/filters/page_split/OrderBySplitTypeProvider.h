// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_ORDERBYSPLITTYPEPROVIDER_H_
#define SCANTAILOR_PAGE_SPLIT_ORDERBYSPLITTYPEPROVIDER_H_

#include <memory>
#include <vector>

#include "PageOrderProvider.h"
#include "Settings.h"

namespace page_split {
class OrderBySplitTypeProvider : public PageOrderProvider {
 public:
  explicit OrderBySplitTypeProvider(std::shared_ptr<Settings> settings);

  bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override;

  Comparator snapshot(const std::vector<PageId>& pages) const override;

 private:
  /** The layout type to order by; unknown ones (AUTO_LAYOUT_TYPE) come last. */
  static int layoutTypeOf(const Settings& settings, const PageId& page);

  static bool compare(int lhsLayoutType,
                      bool lhsIncomplete,
                      const PageId& lhsPage,
                      int rhsLayoutType,
                      bool rhsIncomplete,
                      const PageId& rhsPage);

  std::shared_ptr<Settings> m_settings;
};
}  // namespace page_split

#endif  // SCANTAILOR_PAGE_SPLIT_ORDERBYSPLITTYPEPROVIDER_H_
