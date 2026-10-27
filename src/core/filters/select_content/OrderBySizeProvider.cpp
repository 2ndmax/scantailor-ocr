// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderBySizeProvider.h"

#include <utility>

namespace select_content {
OrderBySizeProvider::OrderBySizeProvider(std::shared_ptr<Settings> settings, const Dimension dimension)
    : OrderBySizeProviderBase([settings = std::move(settings)](const PageId& page) { return sizeOf(*settings, page); },
                              dimension) {}

QSizeF OrderBySizeProvider::sizeOf(const Settings& settings, const PageId& page) {
  const std::unique_ptr<Params> params(settings.getPageParams(page));
  return params ? params->contentSizeMM() : QSizeF();
}
}  // namespace select_content