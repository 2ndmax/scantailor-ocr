// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_ORDERBYSIZEPROVIDER_H_
#define SCANTAILOR_SELECT_CONTENT_ORDERBYSIZEPROVIDER_H_

#include <memory>

#include "PageOrderProvider.h"
#include "Settings.h"

namespace select_content {
/** Orders pages by the width or the height of their content box. */
class OrderBySizeProvider : public PageOrderProvider {
 public:
  enum Dimension { WIDTH, HEIGHT };

  OrderBySizeProvider(std::shared_ptr<Settings> settings, Dimension dimension);

  bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override;

 private:
  std::shared_ptr<Settings> m_settings;
  Dimension m_dimension;
};
}  // namespace select_content
#endif