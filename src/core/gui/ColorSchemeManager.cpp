// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorSchemeManager.h"

#include <QApplication>
#include <cassert>

#include "NativeColorScheme.h"

ColorSchemeManager& ColorSchemeManager::instance() {
  static ColorSchemeManager instance;
  return instance;
}

void ColorSchemeManager::setColorScheme(const ColorScheme& colorScheme) {
  if (QStyle* style = colorScheme.getStyle()) {
    qApp->setStyle(style);
  }
  if (const QPalette* palette = colorScheme.getPalette()) {
    qApp->setPalette(*palette);
  }
  if (const QString* styleSheet = colorScheme.getStyleSheet()) {
    qApp->setStyleSheet(*styleSheet);
  }
  m_roundedCorners = colorScheme.hasRoundedCorners();
  if (const ColorScheme::ColorParams* colorParams = colorScheme.getColorParams()) {
    m_colorParams = std::make_unique<ColorScheme::ColorParams>(*colorParams);
  }
}

QColor ColorSchemeManager::getColorParam(const QString& colorParam) const {
  if (!m_colorParams) {
    m_colorParams = std::make_unique<ColorScheme::ColorParams>(*NativeColorScheme().getColorParams());
  }
  const auto it = m_colorParams->find(colorParam);
  assert((it != m_colorParams->end()) && "Every color scheme sets every color parameter.");
  return (it != m_colorParams->end()) ? it->second : QColor();
}
