// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderBySizeProviderBase.h"

#include <utility>

OrderBySizeProviderBase::OrderBySizeProviderBase(SizeOf sizeOf, const Dimension dimension)
    : m_sizeOf(std::move(sizeOf)), m_dimension(dimension) {}

bool OrderBySizeProviderBase::compare(const Dimension dimension,
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

bool OrderBySizeProviderBase::precedes(const PageId& lhsPage,
                                       const bool lhsIncomplete,
                                       const PageId& rhsPage,
                                       const bool rhsIncomplete) const {
  return compare(m_dimension, m_sizeOf(lhsPage), lhsIncomplete, m_sizeOf(rhsPage), rhsIncomplete);
}

PageOrderProvider::Comparator OrderBySizeProviderBase::snapshot(const std::vector<PageId>& pages) const {
  return snapshotOf<QSizeF>(pages, m_sizeOf,
                            [dimension = m_dimension](const QSizeF& lhsSize, const bool lhsIncomplete, const PageId&,
                                                      const QSizeF& rhsSize, const bool rhsIncomplete, const PageId&) {
                              return compare(dimension, lhsSize, lhsIncomplete, rhsSize, rhsIncomplete);
                            });
}
