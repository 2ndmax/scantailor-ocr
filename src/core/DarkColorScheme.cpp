// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DarkColorScheme.h"

namespace {
StyleSheetColorScheme::Colors darkColors() {
  namespace table = dark_scheme_colors;
  StyleSheetColorScheme::Colors colors;
  colors.window = QColor(table::window);
  colors.content = QColor(table::content);
  colors.border = QColor(table::border);
  colors.text = QColor(table::text);
  colors.disabledText = QColor(table::disabledText);
  colors.textOnAccent = QColor(table::textOnAccent);
  colors.error = QColor(table::error);
  colors.link = QColor(table::link);
  colors.imageView = QColor(table::imageView);
  colors.inactiveSelection = QColor(table::inactiveSelection);
  return colors;
}
}  // namespace

DarkColorScheme::DarkColorScheme()
    : StyleSheetColorScheme(darkColors(),
                            QString::fromUtf8(":/dark_scheme/stylesheet/stylesheet.qss"),
                            DEFAULT_ACCENT_COLOR) {}
