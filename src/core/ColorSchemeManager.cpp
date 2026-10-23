// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorSchemeManager.h"

#include <QApplication>

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
    m_roundedCorners = !styleSheet->contains(QLatin1String("QPushButton"));
  }
  if (const ColorScheme::ColorParams* colorParams = colorScheme.getColorParams()) {
    m_colorParams = std::make_unique<ColorScheme::ColorParams>(*colorParams);
  }
}

QBrush ColorSchemeManager::getColorParam(const QString& colorParam, const QBrush& defaultBrush) const {
  const QColor* color = findColorParam(colorParam);
  return color ? QBrush(*color, defaultBrush.style()) : defaultBrush;
}

QColor ColorSchemeManager::getColorParam(const QString& colorParam, const QColor& defaultColor) const {
  const QColor* color = findColorParam(colorParam);
  return color ? *color : defaultColor;
}

const QColor* ColorSchemeManager::findColorParam(const QString& colorParam) const {
  if (!m_colorParams) {
    return nullptr;
  }
  const auto it = m_colorParams->find(colorParam);
  return (it != m_colorParams->end()) ? &it->second : nullptr;
}
