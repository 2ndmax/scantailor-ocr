// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_
#define SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_

#include <foundation/NonCopyable.h>

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

  /**
   * \brief A color for an element that doesn't support styling, as the color scheme sets it.
   *
   * Every color scheme sets every parameter. Before a scheme is set, as in tests, the colors of
   * the native scheme are used.
   */
  QColor getColorParam(const QString& colorParam) const;

  /**
   * Whether the color scheme draws buttons, fields and frames with rounded corners, as the
   * Windows 11 style does, rather than with square ones, as the style sheets of the light and
   * dark color schemes do. Frames the program draws itself should follow it.
   */
  bool hasRoundedCorners() const { return m_roundedCorners; }

 private:
  mutable std::unique_ptr<ColorScheme::ColorParams> m_colorParams;
  bool m_roundedCorners = true;
};


#endif  // SCANTAILOR_CORE_COLORSCHEMEMANAGER_H_
