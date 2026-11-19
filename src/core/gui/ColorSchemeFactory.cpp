// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorSchemeFactory.h"

#include "ApplicationSettings.h"
#include "ColorScheme.h"
#include "NativeColorScheme.h"
#include "StyleSheetColorScheme.h"
#include "color_schemes/dark_scheme_colors.h"
#include "color_schemes/light_scheme_colors.h"

ColorSchemeFactory::ColorSchemeFactory() {
  registerColorScheme("native", []() { return std::make_unique<NativeColorScheme>(); });
  registerColorScheme("light", []() { return StyleSheetColorScheme::of<light_scheme_colors>(); });
  registerColorScheme("dark", []() { return StyleSheetColorScheme::of<dark_scheme_colors>(); });
}

void ColorSchemeFactory::registerColorScheme(const QString& scheme, const ColorSchemeConstructor& constructor) {
  m_registry[scheme] = constructor;
}

std::unique_ptr<ColorScheme> ColorSchemeFactory::create(const QString& scheme) const {
  // An unknown name, e.g. from an edited settings file, falls back to the default scheme.
  const auto it = m_registry.find(scheme);
  if (it == m_registry.end()) {
    return m_registry.at(ApplicationSettings::DEFAULT_COLOR_SCHEME)();
  }
  return it->second();
}
