// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DARKCOLORSCHEME_H_
#define SCANTAILOR_CORE_DARKCOLORSCHEME_H_

#include "StyleSheetColorScheme.h"
#include "color_schemes/dark_scheme_colors.h"

class DarkColorScheme : public StyleSheetColorScheme {
 public:
  /** The color of the borders of focused and hovered controls, unless the user chose another one. */
  static constexpr const char* DEFAULT_ACCENT_COLOR = dark_scheme_colors::accent;

  DarkColorScheme();
};

#endif  // SCANTAILOR_CORE_DARKCOLORSCHEME_H_
