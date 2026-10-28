// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderBySizeProvider.h"

#include <utility>

#include "Params.h"

namespace page_layout {
OrderBySizeProvider::OrderBySizeProvider(std::shared_ptr<Settings> settings, const Dimension dimension)
    : OrderBySizeProviderBase([settings = std::move(settings)](const PageId& page) { return sizeOf(*settings, page); },
                              dimension) {}

QSizeF OrderBySizeProvider::sizeOf(const Settings& settings, const PageId& page) {
  const std::unique_ptr<Params> params(settings.getPageParams(page));
  if (!params) {
    return QSizeF();
  }
  QSizeF size = params->contentSizeMM();
  // An unknown content size (-1 x -1) plus the margins would look like a small, known one.
  if (size.isValid()) {
    const Margins margins(params->hardMarginsMM());
    size += QSizeF(margins.left() + margins.right(), margins.top() + margins.bottom());
  }
  return size;
}
}  // namespace page_layout
