// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderByDeviationProvider.h"

OrderByDeviationProvider::OrderByDeviationProvider(const DeviationProvider<PageId>& deviationProvider)
    : m_deviationProvider(&deviationProvider) {}

bool OrderByDeviationProvider::precedes(const PageId& lhsPage,
                                        bool lhsIncomplete,
                                        const PageId& rhsPage,
                                        bool rhsIncomplete) const {
  if (lhsIncomplete != rhsIncomplete) {
    // Pages we don't have a deviation value for yet go to the back.
    return rhsIncomplete;
  }
  return (m_deviationProvider->getDeviationValue(lhsPage) > m_deviationProvider->getDeviationValue(rhsPage));
}
