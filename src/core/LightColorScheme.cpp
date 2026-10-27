// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LightColorScheme.h"

namespace {
StyleSheetColorScheme::Colors lightColors() {
  namespace table = light_scheme_colors;
  StyleSheetColorScheme::Colors colors;
  colors.window = QColor(table::window);
  colors.content = QColor(table::content);
  colors.border = QColor(table::border);
  colors.text = QColor(table::text);
  colors.disabledText = QColor(table::disabledText);
  colors.lightTextOnAccent = QColor(table::lightTextOnAccent);
  colors.darkTextOnAccent = QColor(table::darkTextOnAccent);
  colors.error = QColor(table::error);
  colors.link = QColor(table::link);
  colors.imageView = QColor(table::imageView);
  colors.inactiveSelection = QColor(table::inactiveSelection);
  return colors;
}
}  // namespace

LightColorScheme::LightColorScheme()
    : StyleSheetColorScheme(lightColors(),
                            QString::fromUtf8(":/light_scheme/stylesheet/stylesheet.qss"),
                            DEFAULT_ACCENT_COLOR) {}
