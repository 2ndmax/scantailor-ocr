// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StyleSheetColorScheme.h"

#include <QFile>
#include <QRegularExpression>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>
#include <cmath>

#include "ApplicationSettings.h"
#include "FocusFrameStyle.h"

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

QString qssConvertPxToEm(const QString& stylesheet, const double base, const int precision) {
  QString result;
  const QRegularExpression pxToEm(R"((\d+(\.\d+)?)px)");

  QRegularExpressionMatchIterator iter = pxToEm.globalMatch(stylesheet);
  int prevIndex = 0;
  while (iter.hasNext()) {
    const QRegularExpressionMatch match = iter.next();
    result.append(stylesheet.mid(prevIndex, match.capturedStart() - prevIndex));

    double value = match.captured(1).toDouble();
    value /= base;
    result.append(QString::number(value, 'f', precision)).append(QLatin1String("em"));

    prevIndex = match.capturedEnd();
  }
  result.append(stylesheet.mid(prevIndex));
  return result;
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
  loadStyleSheet(styleSheetPath, colors, onAccent == colors.lightTextOnAccent);
  m_customColors = colorParams(colors.text, colors.border, colors.imageView, colors.error, accent, onAccent);
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
  QStyle* fusion = QStyleFactory::create("Fusion");
  return fusion ? new FocusFrameStyle(fusion) : nullptr;
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

QString StyleSheetColorScheme::sharedComponentRules(const QString& tabBackground,
                                                    const QString& tabHoverBackground,
                                                    const QString& separatorBackground,
                                                    const QString& groupBorder,
                                                    const bool tabBorder) {
  QString rules;
  if (tabBorder) {
    rules.append(
        QStringLiteral("QTabWidget[outputTabs=\"true\"]::tab-bar {\n"
                       "  left: 0px;\n"
                       "  right: 0px;\n"
                       "  top: 0px;\n"
                       "}\n\n"
                       "QTabWidget[outputTabs=\"true\"] > QTabBar::tab {\n"
                       "  padding: 9px 3px 9px 3px;\n"
                       "  min-height: 17px;\n"
                       "  min-width: 17px;\n"
                       "  margin: 2px 0px 2px 0px;\n"
                       "}\n\n"
                       "QTabWidget[outputTabs=\"true\"] > QTabBar::tab:!first,\n"
                       "QTabWidget[outputTabs=\"true\"] > QTabBar::tab:selected {\n"
                       "  border-width: 1px;\n"
                       "  padding: 9px 3px 9px 3px;\n"
                       "}\n\n"
                       "QTabWidget[outputTabs=\"true\"] > QTabBar::tab:selected {\n"
                       "  color: palette(highlighted-text);\n"
                       "  background-color: palette(highlight);\n"
                       "  border-color: palette(highlight);\n"
                       "}\n\n"));
  } else {
    rules.append(QStringLiteral("QTabWidget[outputTabs=\"true\"] > QTabBar::tab {\n"
                                "  padding: 9px 3px 9px 3px;\n"
                                "  min-height: 17px;\n"
                                "  min-width: 17px;\n"
                                "  margin: 2px 0px 2px 0px;\n"
                                "  border: none;\n"
                                "  background-color: %1;\n"
                                "}\n\n"
                                "QTabWidget[outputTabs=\"true\"] > QTabBar::tab:hover {\n"
                                "  background-color: %2;\n"
                                "}\n\n"
                                "QTabWidget[outputTabs=\"true\"] > QTabBar::tab:selected {\n"
                                "  color: palette(highlighted-text);\n"
                                "  background-color: palette(highlight);\n"
                                "}\n\n")
                     .arg(tabBackground, tabHoverBackground));
  }

  rules.append(QStringLiteral("QMainWindow::separator {\n"
                              "  background-color: %1;\n"
                              "  width: 5px;\n"
                              "  height: 5px;\n"
                              "}\n\n"
                              "QMainWindow::separator:hover {\n"
                              "  background-color: palette(highlight);\n"
                              "}\n\n"
                              "QGroupBox#recentProjectsGroup {\n"
                              "  background-color: transparent;\n"
                              "  border: none;\n"
                              "  border-top: 1px solid %2;\n"
                              "  margin-top: 0.75em;\n"
                              "  padding-top: 0.5em;\n"
                              "  color: palette(window-text);\n"
                              "}\n\n"
                              "QGroupBox#recentProjectsGroup::title {\n"
                              "  subcontrol-origin: margin;\n"
                              "  subcontrol-position: top center;\n"
                              "  padding: 0px 4px 0px 4px;\n"
                              "  background-color: palette(window);\n"
                              "}\n")
                   .arg(separatorBackground, groupBorder));
  return rules;
}

void StyleSheetColorScheme::loadStyleSheet(const QString& path, const Colors& colors, const bool lightMarks) {
  QFile styleSheetFile(path);
  if (styleSheetFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
    m_styleSheet = styleSheetFile.readAll();
    styleSheetFile.close();
  }
  // The marks on the accent color exist with light and with dark text color (cmake/ColorSchemes.cmake).
  m_styleSheet.replace(QStringLiteral("/icons/marks/"),
                       lightMarks ? QStringLiteral("/icons/marks_light/") : QStringLiteral("/icons/marks_dark/"));
  m_styleSheet.append(QLatin1Char('\n'));
  m_styleSheet.append(sharedComponentRules({}, {}, colors.bar.name(), colors.border.name(), true));

#ifdef _WIN32
  m_styleSheet = qssConvertPxToEm(m_styleSheet, 13, 4);
#else
  m_styleSheet = qssConvertPxToEm(m_styleSheet, 16, 4);
#endif
}

ColorScheme::ColorParams StyleSheetColorScheme::colorParams(const QColor& text,
                                                            const QColor& border,
                                                            const QColor& imageView,
                                                            const QColor& error,
                                                            const QColor& accent,
                                                            const QColor& onAccent) {
  ColorParams params;
  params["ThumbnailSequenceItemText"] = text;
  params["ThumbnailSequenceSelectedItemBorder"] = accent;
  params["ThumbnailSequenceSelectionLeaderBackground"] = accent;
  params["ThumbnailSequenceSelectionLeaderText"] = onAccent;
  params["OpenNewProjectBorder"] = border;
  params["ProcessingIndicationFade"] = imageView;
  params["ProcessingIndicationHead"] = text;
  params["ProcessingIndicationTail"] = accent;
  params["StageListHead"] = text;
  params["StageListTail"] = accent;
  params["ErrorText"] = error;
  return params;
}
