// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_FOCUSFRAMESTYLE_H_
#define SCANTAILOR_CORE_FOCUSFRAMESTYLE_H_

#include <QProxyStyle>

/**
 * The style of the Light and Dark schemes: Fusion with its own keyboard focus frame.
 *
 * Fusion marks keyboard focus with a tinted rectangle around the content, which on the buttons of
 * the style sheet covers only their middle. This style draws a dashed frame in the accent color
 * instead: just inside the border of push and tool buttons, around the content of other widgets. Like
 * Fusion, it draws nothing when the focus came from a mouse click.
 */
class FocusFrameStyle : public QProxyStyle {
 public:
  /** Takes ownership of the base style. */
  explicit FocusFrameStyle(QStyle* baseStyle);

  void drawPrimitive(PrimitiveElement element,
                     const QStyleOption* option,
                     QPainter* painter,
                     const QWidget* widget) const override;

  void drawControl(ControlElement element,
                   const QStyleOption* option,
                   QPainter* painter,
                   const QWidget* widget) const override;
};

#endif  // SCANTAILOR_CORE_FOCUSFRAMESTYLE_H_
