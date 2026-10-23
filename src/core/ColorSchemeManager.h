// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_
#define SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_

#include <foundation/NonCopyable.h>

#include <QBrush>
#include <QColor>
#include <memory>

#include "ColorScheme.h"

class ColorSchemeManager {
  DECLARE_NON_COPYABLE(ColorSchemeManager)
 private:
  ColorSchemeManager() = default;

 public:
  static ColorSchemeManager& instance();

  void setColorScheme(const ColorScheme& colorScheme);

  QBrush getColorParam(const QString& colorParam, const QBrush& defaultBrush) const;

  QColor getColorParam(const QString& colorParam, const QColor& defaultColor) const;

  /**
   * Whether the color scheme draws buttons, fields and frames with rounded corners, as the
   * Windows 11 style does, rather than with square ones, as the style sheets of the light and
   * dark color schemes do. Frames the program draws itself should follow it.
   */
  bool hasRoundedCorners() const { return m_roundedCorners; }

 private:
  /** The colour set for \p colorParam by the colour scheme, or null. */
  const QColor* findColorParam(const QString& colorParam) const;

  std::unique_ptr<ColorScheme::ColorParams> m_colorParams;
  bool m_roundedCorners = true;
};


#endif  // SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_
