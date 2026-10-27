// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DarkColorScheme.h"

namespace {
StyleSheetColorScheme::BaseColors darkColors() {
  StyleSheetColorScheme::BaseColors colors;
  colors.window = QColor("#535353");
  colors.windowText = QColor("#DDDDDD");
  colors.disabledWindowText = QColor("#989898");
  colors.base = QColor("#454545");
  colors.disabledBase = QColor("#4D4D4D");
  colors.toolTipBase = QColor("#707070");
  colors.toolTipText = colors.windowText;
  colors.light = QColor("#666666");
  colors.midlight = QColor("#535353");
  colors.dark = QColor("#404040");
  colors.mid = QColor("#333333");
  colors.shadow = QColor("#262626");
  colors.brightText = QColor("#FC5248");
  colors.link = QColor("#4F95FC");
  colors.highlight = QColor("#6B6B6B");
  colors.disabledHighlight = QColor("#5D5D5D");
  return colors;
}
}  // namespace

DarkColorScheme::DarkColorScheme()
    : StyleSheetColorScheme(darkColors(),
                            QString::fromUtf8(":/dark_scheme/stylesheet/stylesheet.qss"),
                            DEFAULT_ACCENT_COLOR) {
  setColorParam("ThumbnailSequenceSelectedItemBackground", QColor("#424242"));
  setColorParam("ThumbnailSequenceSelectedItemText", QColor("#DDDDDD"));
  setColorParam("ThumbnailSequenceItemText", QColor("#DDDDDD"));
  setColorParam("ThumbnailSequenceSelectionLeaderBackground", QColor("#7A7A7A"));
  setColorParam("ThumbnailSequenceSelectionLeaderText", Qt::white);
  setColorParam("OpenNewProjectBorder", QColor("#535353"));
  setColorParam("ProcessingIndicationFade", QColor("#282828"));
  setColorParam("FixDpiDialogErrorText", QColor("#F34941"));
}
