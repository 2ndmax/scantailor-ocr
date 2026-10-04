// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderByCompletenessProvider.h"

bool OrderByCompletenessProvider::precedes(const PageId&, bool lhsIncomplete, const PageId&, bool rhsIncomplete) const {
  if (lhsIncomplete != rhsIncomplete) {
    // Incomplete pages go to the back.
    return rhsIncomplete;
  }
  // Equally complete pages are equivalent. Returning true here would make precedes(a, b) and
  // precedes(b, a) both true, which is not the strict weak ordering the sort requires, and
  // would shuffle pages that ought to keep their natural order.
  return false;
}
