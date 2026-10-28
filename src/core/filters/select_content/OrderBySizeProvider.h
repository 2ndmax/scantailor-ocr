// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_ORDERBYSIZEPROVIDER_H_
#define SCANTAILOR_SELECT_CONTENT_ORDERBYSIZEPROVIDER_H_

#include <QSizeF>
#include <memory>

#include "OrderBySizeProviderBase.h"
#include "Settings.h"

namespace select_content {
/** Orders pages by the width or the height of their content box. */
class OrderBySizeProvider : public OrderBySizeProviderBase {
 public:
  OrderBySizeProvider(std::shared_ptr<Settings> settings, Dimension dimension);

 private:
  /** The size of the content box; invalid if unknown. */
  static QSizeF sizeOf(const Settings& settings, const PageId& page);
};
}  // namespace select_content
#endif
