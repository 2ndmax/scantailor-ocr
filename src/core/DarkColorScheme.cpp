// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DarkColorScheme.h"

namespace colors = dark_scheme_colors;

namespace {
StyleSheetColorScheme::BaseColors darkColors() {
  StyleSheetColorScheme::BaseColors base;
  base.window = QColor(colors::window);
  base.windowText = QColor(colors::windowText);
  base.disabledWindowText = QColor(colors::disabledWindowText);
  base.base = QColor(colors::base);
  base.disabledBase = QColor(colors::disabledBase);
  base.toolTipBase = QColor(colors::toolTipBase);
  base.toolTipText = QColor(colors::toolTipText);
  base.light = QColor(colors::light);
  base.midlight = QColor(colors::midlight);
  base.dark = QColor(colors::dark);
  base.mid = QColor(colors::mid);
  base.shadow = QColor(colors::shadow);
  base.brightText = QColor(colors::brightText);
  base.link = QColor(colors::link);
  base.highlight = QColor(colors::highlight);
  base.disabledHighlight = QColor(colors::disabledHighlight);
  return base;
}
}  // namespace

DarkColorScheme::DarkColorScheme()
    : StyleSheetColorScheme(darkColors(),
                            QString::fromUtf8(":/dark_scheme/stylesheet/stylesheet.qss"),
                            DEFAULT_ACCENT_COLOR) {
  setColorParam("ThumbnailSequenceItemText", QColor(colors::thumbnailItemText));
  setColorParam("ThumbnailSequenceSelectionLeaderBackground", QColor(colors::thumbnailSelectionLeaderBackground));
  setColorParam("ThumbnailSequenceSelectionLeaderText", QColor(colors::thumbnailSelectionLeaderText));
  setColorParam("OpenNewProjectBorder", QColor(colors::openNewProjectBorder));
  setColorParam("ProcessingIndicationFade", QColor(colors::processingIndicationFade));
  setColorParam("FixDpiDialogErrorText", QColor(colors::fixDpiDialogErrorText));
}
