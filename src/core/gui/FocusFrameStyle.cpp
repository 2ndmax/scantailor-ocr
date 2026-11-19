// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FocusFrameStyle.h"

#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QStyleOptionFocusRect>
#include <QToolButton>

FocusFrameStyle::FocusFrameStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

void FocusFrameStyle::drawPrimitive(const PrimitiveElement element,
                                    const QStyleOption* option,
                                    QPainter* painter,
                                    const QWidget* widget) const {
  if (element != PE_FrameFocusRect) {
    QProxyStyle::drawPrimitive(element, option, painter, widget);
    return;
  }
  // The flag is only set while the focus is moved by keyboard; a mouse click clears it.
  if (!(option->state & State_KeyboardFocusChange)) {
    return;
  }

  // On buttons the frame runs just inside their border, which stays free for its own marks: the
  // accent border of a checked button, the wider accent frame of the Export button (2 px).
  const bool button = qobject_cast<const QPushButton*>(widget) || qobject_cast<const QToolButton*>(widget);
  const QRect frame = (button ? widget->rect().adjusted(2, 2, -2, -2) : option->rect).adjusted(0, 0, -1, -1);
  painter->save();
  painter->setBrush(Qt::NoBrush);
  QPen dashes(option->palette.color(QPalette::Highlight), 1);
  dashes.setDashPattern({2, 2});
  painter->setPen(dashes);
  painter->drawRect(frame);
  painter->restore();
}

void FocusFrameStyle::drawControl(const ControlElement element,
                                  const QStyleOption* option,
                                  QPainter* painter,
                                  const QWidget* widget) const {
  QProxyStyle::drawControl(element, option, painter, widget);
  // The style sheet draws tool buttons without asking for a focus frame, so it is added after
  // their label, the last part of a tool button the style draws.
  if ((element == CE_ToolButtonLabel) && qobject_cast<const QToolButton*>(widget) && (option->state & State_HasFocus)) {
    QStyleOptionFocusRect focusOption;
    focusOption.QStyleOption::operator=(*option);
    painter->save();
    painter->setClipping(false);
    drawPrimitive(PE_FrameFocusRect, &focusOption, painter, widget);
    painter->restore();
  }
}
