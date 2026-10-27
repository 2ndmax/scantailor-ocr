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
 * Both are built the same way from the same meanings of colors; the subclasses only give the colors.
 */
class StyleSheetColorScheme : public ColorScheme {
 public:
  /**
   * The colors of the scheme that the palette and the color parameters are made of, one for each
   * meaning (see src/resources/color_schemes/light.colors).
   */
  struct Colors {
    QColor window;
    QColor content;
    QColor border;
    QColor text;
    QColor disabledText;
    QColor textOnAccent;
    QColor error;
    QColor link;
    QColor imageView;
    QColor inactiveSelection;
  };

  QStyle* getStyle() const override;

  const QPalette* getPalette() const override;

  const QString* getStyleSheet() const override;

  const ColorParams* getColorParams() const override;

 protected:
  /**
   * \param styleSheetPath The style sheet in the resources.
   * \param defaultAccentColor The accent color unless the user chose another one.
   */
  StyleSheetColorScheme(const Colors& colors, const QString& styleSheetPath, const char* defaultAccentColor);

 private:
  void loadPalette(const Colors& colors, const QColor& accent);

  void loadStyleSheet(const QString& path);

  void loadColorParams(const Colors& colors, const QColor& accent);

  QPalette m_palette;
  QString m_styleSheet;
  ColorParams m_customColors;
};

#endif  // SCANTAILOR_CORE_STYLESHEETCOLORSCHEME_H_
