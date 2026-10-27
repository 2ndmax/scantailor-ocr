// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StyleSheetColorScheme.h"

#include <QFile>
#include <QStyle>
#include <QStyleFactory>

#include "ApplicationSettings.h"
#include "Utils.h"

using namespace core;

StyleSheetColorScheme::StyleSheetColorScheme(const BaseColors& colors,
                                             const QString& styleSheetPath,
                                             const char* defaultAccentColor) {
  loadPalette(colors);
  loadStyleSheet(styleSheetPath, defaultAccentColor);
  loadCommonColorParams();
}

const QPalette* StyleSheetColorScheme::getPalette() const {
  return &m_palette;
}

const QString* StyleSheetColorScheme::getStyleSheet() const {
  return &m_styleSheet;
}

const ColorScheme::ColorParams* StyleSheetColorScheme::getColorParams() const {
  return &m_customColors;
}

QStyle* StyleSheetColorScheme::getStyle() const {
  return QStyleFactory::create("Fusion");
}

void StyleSheetColorScheme::setColorParam(const QString& name, const QColor& color) {
  m_customColors[name] = color;
}

void StyleSheetColorScheme::loadPalette(const BaseColors& colors) {
  m_palette.setColor(QPalette::Window, colors.window);
  m_palette.setColor(QPalette::WindowText, colors.windowText);
  m_palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.disabledWindowText);
  m_palette.setColor(QPalette::Base, colors.base);
  m_palette.setColor(QPalette::Disabled, QPalette::Base, colors.disabledBase);
  m_palette.setColor(QPalette::AlternateBase, m_palette.color(QPalette::Window));
  m_palette.setColor(QPalette::Disabled, QPalette::AlternateBase,
                     m_palette.color(QPalette::Disabled, QPalette::Window));
  m_palette.setColor(QPalette::ToolTipBase, colors.toolTipBase);
  m_palette.setColor(QPalette::ToolTipText, colors.toolTipText);
  m_palette.setColor(QPalette::Text, m_palette.color(QPalette::WindowText));
  m_palette.setColor(QPalette::Disabled, QPalette::Text, m_palette.color(QPalette::Disabled, QPalette::WindowText));
  m_palette.setColor(QPalette::Light, colors.light);
  m_palette.setColor(QPalette::Midlight, colors.midlight);
  m_palette.setColor(QPalette::Dark, colors.dark);
  m_palette.setColor(QPalette::Mid, colors.mid);
  m_palette.setColor(QPalette::Shadow, colors.shadow);
  m_palette.setColor(QPalette::Button, m_palette.color(QPalette::Base));
  m_palette.setColor(QPalette::Disabled, QPalette::Button, m_palette.color(QPalette::Disabled, QPalette::Base));
  m_palette.setColor(QPalette::ButtonText, m_palette.color(QPalette::WindowText));
  m_palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                     m_palette.color(QPalette::Disabled, QPalette::WindowText));
  m_palette.setColor(QPalette::BrightText, colors.brightText);
  m_palette.setColor(QPalette::Link, colors.link);
  m_palette.setColor(QPalette::Highlight, colors.highlight);
  m_palette.setColor(QPalette::Disabled, QPalette::Highlight, colors.disabledHighlight);
  m_palette.setColor(QPalette::HighlightedText, m_palette.color(QPalette::WindowText));
  m_palette.setColor(QPalette::Disabled, QPalette::HighlightedText,
                     m_palette.color(QPalette::Disabled, QPalette::WindowText));
}

void StyleSheetColorScheme::loadStyleSheet(const QString& path, const char* defaultAccentColor) {
  QFile styleSheetFile(path);
  if (styleSheetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    m_styleSheet = styleSheetFile.readAll();
    styleSheetFile.close();
  }
  m_styleSheet = Utils::qssApplyAccentColor(m_styleSheet, defaultAccentColor);

#ifdef _WIN32
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 13, 4);
#else
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 16, 4);
#endif
}

void StyleSheetColorScheme::loadCommonColorParams() {
  const QColor accent = ApplicationSettings::getInstance().getAccentColor();
  m_customColors["ThumbnailSequenceSelectedItemBorder"] = accent.isValid() ? accent : QColor("#3399ff");
  m_customColors["ProcessingIndicationHead"] = m_palette.color(QPalette::WindowText);
  m_customColors["ProcessingIndicationTail"] = m_palette.color(QPalette::Highlight);
  m_customColors["StageListHead"] = m_customColors.at("ProcessingIndicationHead");
  m_customColors["StageListTail"] = m_customColors.at("ProcessingIndicationTail");
}
