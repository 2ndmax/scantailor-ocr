// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderBySizeProvider.h"

#include <utility>

namespace select_content {
OrderBySizeProvider::OrderBySizeProvider(std::shared_ptr<Settings> settings, const Dimension dimension)
    : m_settings(std::move(settings)), m_dimension(dimension) {}

QSizeF OrderBySizeProvider::sizeOf(const Settings& settings, const PageId& page) {
  const std::unique_ptr<Params> params(settings.getPageParams(page));
  return params ? params->contentSizeMM() : QSizeF();
}

bool OrderBySizeProvider::compare(const Dimension dimension,
                                  const QSizeF& lhsSize,
                                  const bool lhsIncomplete,
                                  const QSizeF& rhsSize,
                                  const bool rhsIncomplete) {
  const bool lhsValid = !lhsIncomplete && lhsSize.isValid();
  const bool rhsValid = !rhsIncomplete && rhsSize.isValid();

  if (lhsValid != rhsValid) {
    // Invalid (unknown) sizes go to the back.
    return lhsValid;
  }
  if (dimension == WIDTH) {
    return lhsSize.width() < rhsSize.width();
  }
  return lhsSize.height() < rhsSize.height();
}

bool OrderBySizeProvider::precedes(const PageId& lhsPage,
                                   const bool lhsIncomplete,
                                   const PageId& rhsPage,
                                   const bool rhsIncomplete) const {
  return compare(m_dimension, sizeOf(*m_settings, lhsPage), lhsIncomplete, sizeOf(*m_settings, rhsPage), rhsIncomplete);
}

PageOrderProvider::Comparator OrderBySizeProvider::snapshot(const std::vector<PageId>& pages) const {
  return snapshotOf<QSizeF>(
      pages, [settings = m_settings](const PageId& page) { return sizeOf(*settings, page); },
      [dimension = m_dimension](const QSizeF& lhsSize, const bool lhsIncomplete, const PageId&, const QSizeF& rhsSize,
                                const bool rhsIncomplete, const PageId&) {
        return compare(dimension, lhsSize, lhsIncomplete, rhsSize, rhsIncomplete);
      });
}
}  // namespace select_content