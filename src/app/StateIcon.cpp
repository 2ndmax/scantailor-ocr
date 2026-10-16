// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StateIcon.h"

#include <QGuiApplication>
#include <QPainter>
#include <array>

QPixmap StateIcon::pixmap(const Color color) {
  static const std::array<QPixmap, 4> icons{create(Green), create(Yellow), create(Red), create(None)};
  return icons[color];
}

QPixmap StateIcon::create(const Color color) {
  const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
  const int size = 12;
  QPixmap pixmap(qRound(size * dpr), qRound(size * dpr));
  pixmap.setDevicePixelRatio(dpr);
  pixmap.fill(Qt::transparent);
  if (color == None) {
    return pixmap;
  }

  QColor penColor;
  QColor brushColor;
  switch (color) {
    case Green:
      penColor = QColor(0x3a5827);
      brushColor = QColor(0x89e74a);
      break;
    case Yellow:
      penColor = QColor(0x7a5418);
      brushColor = QColor(0xffc04b);
      break;
    default:
      penColor = QColor(0x6f2719);
      brushColor = QColor(0xff674b);
      break;
  }

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(penColor);
  pen.setWidthF(1.5);
  painter.setPen(pen);
  painter.setBrush(brushColor);
  painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
  return pixmap;
}
