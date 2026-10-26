// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DespeckleLevel.h"

#include <QString>

namespace output {
DespeckleLevel despeckleLevelFromString(const QString& str) {
  if (str == "off") {
    return DESPECKLE_OFF;
  } else if (str == "cautious") {
    return DESPECKLE_CAUTIOUS;
  } else if (str == "aggressive") {
    return DESPECKLE_AGGRESSIVE;
  } else {
    return DESPECKLE_NORMAL;
  }
}
}  // namespace output