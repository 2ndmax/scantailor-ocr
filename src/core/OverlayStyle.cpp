// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OverlayStyle.h"

#include <QGuiApplication>
#include <QPainter>

namespace OverlayStyle {
QColor primary(const int alpha) {
  return QColor(0x1d, 0x73, 0xdf, alpha);
}

QColor secondary(const int alpha) {
  return QColor(0xe0, 0x66, 0x00, alpha);
}

QColor removed() {
  return QColor(0, 0, 0, 80);
}

qreal lineWidth(const Line line) {
  switch (line) {
    case Line::Fine:
      return 2.0;
    case Line::Normal:
      return 3.0;
    case Line::Strong:
      return 4.0;
    case Line::Thumbnail:
      return 1.0;
  }
  return 1.0;
}

QPen pen(const QColor& color, const Line line) {
  QPen result(color, lineWidth(line));
  result.setCosmetic(true);
  return result;
}

QPixmap handlePixmap() {
  const qreal pixelRatio = qApp->devicePixelRatio();
  QPixmap pixmap(QSize(16, 16) * pixelRatio);
  pixmap.setDevicePixelRatio(pixelRatio);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(primary(), 2.0));
  painter.setBrush(Qt::white);
  painter.drawEllipse(QRectF(2.0, 2.0, 12.0, 12.0));
  return pixmap;
}
}  // namespace OverlayStyle
