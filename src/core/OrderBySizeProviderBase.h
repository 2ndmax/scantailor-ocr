// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ORDERBYSIZEPROVIDERBASE_H_
#define SCANTAILOR_CORE_ORDERBYSIZEPROVIDERBASE_H_

#include <QSizeF>
#include <functional>
#include <vector>

#include "PageOrderProvider.h"

/**
 * Orders pages by the width or the height of a size the subclass reads from its settings.
 * Pages with an unknown size and incomplete pages go to the back.
 */
class OrderBySizeProviderBase : public PageOrderProvider {
 public:
  enum Dimension { WIDTH, HEIGHT };

  bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override;

  Comparator snapshot(const std::vector<PageId>& pages) const override;

 protected:
  /** The size a page is ordered by; invalid if unknown. */
  using SizeOf = std::function<QSizeF(const PageId&)>;

  OrderBySizeProviderBase(SizeOf sizeOf, Dimension dimension);

 private:
  static bool compare(Dimension dimension,
                      const QSizeF& lhsSize,
                      bool lhsIncomplete,
                      const QSizeF& rhsSize,
                      bool rhsIncomplete);

  SizeOf m_sizeOf;
  Dimension m_dimension;
};

#endif  // ifndef SCANTAILOR_CORE_ORDERBYSIZEPROVIDERBASE_H_
