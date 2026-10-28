// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include <QLocale>

namespace foundation {
QString Utils::doubleToString(double val) {
  // The shortest form that reads back as the same value. 16 digits weren't always enough,
  // and a page then counted as changed after reopening the project.
  return QString::number(val, 'g', QLocale::FloatingPointShortest);
}
}  // namespace foundation
