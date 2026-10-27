// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StyleSheetColorScheme.h"

#include <QFile>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>
#include <cmath>

#include "ApplicationSettings.h"
#include "Utils.h"

using namespace core;

namespace {
/** The relative luminance of a color, as WCAG 2 defines it for contrast. */
double relativeLuminance(const QColor& color) {
  const auto linear = [](const double channel) {
    return (channel <= 0.03928) ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
}

/** The contrast ratio of two colors, from 1 to 21, as WCAG 2 defines it. */
double contrastRatio(const QColor& first, const QColor& second) {
  const double a = relativeLuminance(first);
  const double b = relativeLuminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}
}  // namespace

StyleSheetColorScheme::StyleSheetColorScheme(const Colors& colors,
                                             const QString& name,
                                             const char* defaultAccentColor) {
  const QString styleSheetPath = QString(":/%1_scheme/stylesheet/stylesheet.qss").arg(name);
  // The palette carries the accent color to the style sheet (palette(highlight)), to selections and to
  // the thumbnails.
  const QColor chosenAccent = ApplicationSettings::getInstance().getAccentColor();
  const QColor accent = chosenAccent.isValid() ? chosenAccent : QColor(defaultAccentColor);
  const QColor onAccent = textOnAccent(accent, colors.lightTextOnAccent, colors.darkTextOnAccent);
  loadPalette(colors, accent, onAccent);
  loadStyleSheet(styleSheetPath, onAccent == colors.lightTextOnAccent);
  m_customColors = colorParams(colors, accent, onAccent);
}

QColor StyleSheetColorScheme::textOnAccent(const QColor& accent, const QColor& lightText, const QColor& darkText) {
  return (contrastRatio(accent, lightText) >= contrastRatio(accent, darkText)) ? lightText : darkText;
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

void StyleSheetColorScheme::loadPalette(const Colors& colors, const QColor& accent, const QColor& onAccent) {
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
  m_palette.setColor(QPalette::HighlightedText, onAccent);
  // Selections of disabled lists and of lists in an inactive window are not in the accent color.
  m_palette.setColor(QPalette::Inactive, QPalette::Highlight, colors.inactiveSelection);
  m_palette.setColor(QPalette::Inactive, QPalette::HighlightedText, colors.text);
  m_palette.setColor(QPalette::Disabled, QPalette::Highlight, colors.inactiveSelection);
  m_palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colors.disabledText);
}

void StyleSheetColorScheme::loadStyleSheet(const QString& path, const bool lightMarks) {
  QFile styleSheetFile(path);
  if (styleSheetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    m_styleSheet = styleSheetFile.readAll();
    styleSheetFile.close();
  }
  // The marks on the accent color exist with light and with dark text color (cmake/ColorSchemes.cmake).
  m_styleSheet.replace(QStringLiteral("/icons/marks/"),
                       lightMarks ? QStringLiteral("/icons/marks_light/") : QStringLiteral("/icons/marks_dark/"));

#ifdef _WIN32
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 13, 4);
#else
  m_styleSheet = Utils::qssConvertPxToEm(m_styleSheet, 16, 4);
#endif
}

ColorScheme::ColorParams StyleSheetColorScheme::colorParams(const Colors& colors,
                                                            const QColor& accent,
                                                            const QColor& onAccent) {
  ColorParams params;
  params["ThumbnailSequenceItemText"] = colors.text;
  params["ThumbnailSequenceSelectedItemBorder"] = accent;
  params["ThumbnailSequenceSelectionLeaderBackground"] = accent;
  params["ThumbnailSequenceSelectionLeaderText"] = onAccent;
  params["OpenNewProjectBorder"] = colors.border;
  params["ProcessingIndicationFade"] = colors.imageView;
  params["ProcessingIndicationHead"] = colors.text;
  params["ProcessingIndicationTail"] = accent;
  params["StageListHead"] = colors.text;
  params["StageListTail"] = accent;
  params["ErrorText"] = colors.error;
  return params;
}
