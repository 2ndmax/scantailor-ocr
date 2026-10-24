// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_ORDERBYSIZEPROVIDER_H_
#define SCANTAILOR_PAGE_LAYOUT_ORDERBYSIZEPROVIDER_H_

#include <QSizeF>
#include <memory>
#include <vector>

#include "PageOrderProvider.h"
#include "Settings.h"

namespace page_layout {
/** Orders pages by the width or the height of their content box with the hard margins. */
class OrderBySizeProvider : public PageOrderProvider {
 public:
  enum Dimension { WIDTH, HEIGHT };

  OrderBySizeProvider(std::shared_ptr<Settings> settings, Dimension dimension);

  bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override;

  Comparator snapshot(const std::vector<PageId>& pages) const override;

 private:
  /** The size with the hard margins; invalid if unknown. */
  static QSizeF sizeOf(const Settings& settings, const PageId& page);

  static bool compare(Dimension dimension,
                      const QSizeF& lhsSize,
                      bool lhsIncomplete,
                      const QSizeF& rhsSize,
                      bool rhsIncomplete);

  std::shared_ptr<Settings> m_settings;
  Dimension m_dimension;
};
}  // namespace page_layout
#endif