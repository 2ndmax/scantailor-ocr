// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LightColorScheme.h"

namespace {
StyleSheetColorScheme::BaseColors lightColors() {
  StyleSheetColorScheme::BaseColors colors;
  colors.window = QColor("#F0F0F0");
  colors.windowText = QColor("#303030");
  colors.disabledWindowText = QColor("#909090");
  colors.base = QColor("#FCFCFC");
  colors.disabledBase = QColor("#FAFAFA");
  colors.toolTipBase = QColor("#FFFFCD");
  colors.toolTipText = Qt::black;
  colors.light = Qt::white;
  colors.midlight = QColor("#F0F0F0");
  colors.dark = QColor("#DADADA");
  colors.mid = QColor("#CCCCCC");
  colors.shadow = QColor("#BEBEBE");
  colors.brightText = QColor("#F40000");
  colors.link = QColor("#0000FF");
  colors.highlight = QColor("#B5B5B5");
  colors.disabledHighlight = QColor("#DFDFDF");
  return colors;
}
}  // namespace

LightColorScheme::LightColorScheme()
    : StyleSheetColorScheme(lightColors(),
                            QString::fromUtf8(":/light_scheme/stylesheet/stylesheet.qss"),
                            DEFAULT_ACCENT_COLOR) {
  setColorParam("ThumbnailSequenceItemText", Qt::black);
  setColorParam("ThumbnailSequenceSelectionLeaderBackground", QColor("#4A4A4A"));
  setColorParam("ThumbnailSequenceSelectionLeaderText", Qt::white);
  setColorParam("OpenNewProjectBorder", QColor("#CCCCCC"));
  setColorParam("ProcessingIndicationFade", QColor("#939393"));
  setColorParam("FixDpiDialogErrorText", QColor("#FB0000"));
}
