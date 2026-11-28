// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StateIcon.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <map>
#include <utility>

namespace {
QImage createImage(const StateIcon::Color color, const qreal dpr) {
  const int size = 12;
  QImage image(qRound(size * dpr), qRound(size * dpr), QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(dpr);
  image.fill(Qt::transparent);
  if (color == StateIcon::None) {
    return image;
  }

  QColor penColor;
  QColor brushColor;
  switch (color) {
    case StateIcon::Green:
      penColor = QColor(0x3a5827);
      brushColor = QColor(0x89e74a);
      break;
    case StateIcon::Yellow:
      penColor = QColor(0x7a5418);
      brushColor = QColor(0xffc04b);
      break;
    default:
      penColor = QColor(0x6f2719);
      brushColor = QColor(0xff674b);
      break;
  }

  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(penColor);
  pen.setWidthF(1.5);
  painter.setPen(pen);
  painter.setBrush(brushColor);
  painter.drawEllipse(QRectF(1, 1, size - 2, size - 2));
  return image;
}
}  // namespace

QPixmap StateIcon::pixmap(const Color color) {
  const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
  const int dprBucket = std::max(100, qRound(dpr * 100.0));
  // Cache QImages (independent of QApplication lifetime) keyed by (color, dpr) so multi-DPI switches stay sharp.
  static std::map<std::pair<int, int>, QImage> imageCache;
  const auto key = std::make_pair(static_cast<int>(color), dprBucket);
  auto it = imageCache.find(key);
  if (it == imageCache.end()) {
    it = imageCache.emplace(key, createImage(color, dprBucket / 100.0)).first;
  }
  return QPixmap::fromImage(it->second);
}
