// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageOrderProvider.h"

PageOrderProvider::Comparator PageOrderProvider::snapshot(const std::vector<PageId>&) const {
  return [this](const PageId& lhsPage, const bool lhsIncomplete, const PageId& rhsPage, const bool rhsIncomplete) {
    return precedes(lhsPage, lhsIncomplete, rhsPage, rhsIncomplete);
  };
}

std::shared_ptr<const PageOrderProvider> PageOrderProvider::reversed() const {
  class ReversedPageOrderProvider : public PageOrderProvider {
   public:
    explicit ReversedPageOrderProvider(const PageOrderProvider* parent) : m_parent(parent->shared_from_this()) {}

    bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override {
      return m_parent->precedes(rhsPage, rhsIncomplete, lhsPage, lhsIncomplete);
    }

    Comparator snapshot(const std::vector<PageId>& pages) const override {
      return [compare = m_parent->snapshot(pages)](const PageId& lhsPage, const bool lhsIncomplete,
                                                   const PageId& rhsPage, const bool rhsIncomplete) {
        return compare(rhsPage, rhsIncomplete, lhsPage, lhsIncomplete);
      };
    }

   private:
    const std::shared_ptr<const PageOrderProvider> m_parent;
  };
  return std::make_shared<ReversedPageOrderProvider>(this);
}
