// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_STYLESHEETCOLORSCHEME_H_
#define SCANTAILOR_CORE_STYLESHEETCOLORSCHEME_H_

#include <QColor>
#include <QPalette>
#include <QString>

#include "ColorScheme.h"

/**
 * A color scheme of the Fusion style with its own palette and style sheet, as Light and Dark are.
 * Both are built the same way; the subclasses only give their colors.
 */
class StyleSheetColorScheme : public ColorScheme {
 public:
  /** The colors the palette is derived from. */
  struct BaseColors {
    QColor window;
    QColor windowText;
    QColor disabledWindowText;
    QColor base;
    QColor disabledBase;
    QColor toolTipBase;
    QColor toolTipText;
    QColor light;
    QColor midlight;
    QColor dark;
    QColor mid;
    QColor shadow;
    QColor brightText;
    QColor link;
    QColor highlight;
    QColor disabledHighlight;
  };

  QStyle* getStyle() const override;

  const QPalette* getPalette() const override;

  const QString* getStyleSheet() const override;

  const ColorParams* getColorParams() const override;

 protected:
  /**
   * \param styleSheetPath The style sheet in the resources.
   * \param defaultAccentColor The accent color in the style sheet; the one the user chose replaces it.
   */
  StyleSheetColorScheme(const BaseColors& colors, const QString& styleSheetPath, const char* defaultAccentColor);

  /** Adds a color for an element that doesn't support styling, beyond the ones both schemes have. */
  void setColorParam(const QString& name, const QColor& color);

 private:
  void loadPalette(const BaseColors& colors);

  void loadStyleSheet(const QString& path, const char* defaultAccentColor);

  void loadCommonColorParams(const char* defaultAccentColor);

  QPalette m_palette;
  QString m_styleSheet;
  ColorParams m_customColors;
};

#endif  // SCANTAILOR_CORE_STYLESHEETCOLORSCHEME_H_
