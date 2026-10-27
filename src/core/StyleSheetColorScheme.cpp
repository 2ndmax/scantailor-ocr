// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StyleSheetColorScheme.h"

#include <QFile>
#include <QStyle>
#include <QStyleFactory>

#include "ApplicationSettings.h"
#include "Utils.h"

using namespace core;

StyleSheetColorScheme::StyleSheetColorScheme(const Colors& colors,
                                             const QString& styleSheetPath,
                                             const char* defaultAccentColor) {
  // The palette carries the accent color to the style sheet (palette(highlight)), to selections and to
  // the thumbnails.
  const QColor chosenAccent = ApplicationSettings::getInstance().getAccentColor();
  const QColor accent = chosenAccent.isValid() ? chosenAccent : QColor(defaultAccentColor);
  loadPalette(colors, accent);
  loadStyleSheet(styleSheetPath);
  loadColorParams(colors, accent);
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

void StyleSheetColorScheme::loadPalette(const Colors& colors, const QColor& accent) {
  m_palette.setColor(QPalette::Window, colors.window);
  m_palette.setColor(QPalette::AlternateBase, colors.window);
  m_palette.setColor(QPalette::Midlight, colors.window);
  m_palette.setColor(QPalette::Base, colors.content);
  m_palette.setColor(QPalette::Button, colors.content);
  m_palette.setColor(QPalette::ToolTipBase, colors.content);
  m_palette.setColor(QPalette::Light, colors.content);
  m_palette.setColor(QPalette::Dark, colors.border);
  m_palette.setColor(QPalette::Mid, colors.border);
  m_palette.setColor(QPalette::Shadow, colors.border);
  m_palette.setColor(QPalette::BrightText, colors.error);
  m_palette.setColor(QPalette::Link, colors.link);
  for (const QPalette::ColorRole role :
       {QPalette::WindowText, QPalette::Text, QPalette::ButtonText, QPalette::ToolTipText}) {
    m_palette.setColor(role, colors.text);
    m_palette.setColor(QPalette::Disabled, role, colors.disabledText);
  }
  m_palette.setColor(QPalette::Highlight, accent);
  m_palette.setColor(QPalette::HighlightedText, colors.textOnAccent);
  // Selections of disabled lists and of lists in an inactive window are not in the accent color.
  m_palette.setColor(QPalette::Inactive, QPalette::Highlight, colors.inactiveSelection);
  m_palette.setColor(QPalette::Inactive, QPalette::HighlightedText, colors.text);
  m_palette.setColor(QPalette::Disabled, QPalette::Highlight, colors.inactiveSelection);
  m_palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colors.disabledText);
}

void StyleSheetColorScheme::loadStyleSheet(const QString& path) {
  QFile styleSheetFile(path);
  if (styleSheetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    m_styleSheet = styleSheetFile.readAll();
    styleSheetFile.close();
  }

#ifdef _WIN32
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 13, 4);
#else
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 16, 4);
#endif
}

void StyleSheetColorScheme::loadColorParams(const Colors& colors, const QColor& accent) {
  m_customColors["ThumbnailSequenceItemText"] = colors.text;
  m_customColors["ThumbnailSequenceSelectedItemBorder"] = accent;
  m_customColors["ThumbnailSequenceSelectionLeaderBackground"] = accent;
  m_customColors["ThumbnailSequenceSelectionLeaderText"] = colors.textOnAccent;
  m_customColors["OpenNewProjectBorder"] = colors.border;
  m_customColors["ProcessingIndicationFade"] = colors.imageView;
  m_customColors["ProcessingIndicationHead"] = colors.text;
  m_customColors["ProcessingIndicationTail"] = accent;
  m_customColors["StageListHead"] = colors.text;
  m_customColors["StageListTail"] = accent;
  m_customColors["FixDpiDialogErrorText"] = colors.error;
}
