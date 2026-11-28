// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PixmapRenderer.h"

#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QRectF>
#include <QTransform>

void PixmapRenderer::drawPixmap(QPainter& painter, const QPixmap& pixmap) {
  if (pixmap.isNull()) {
    return;
  }
  bool ok = false;
  const QTransform invTransform(painter.worldTransform().inverted(&ok));
  if (!ok) {
    return;
  }
  const QRectF srcRect(invTransform.map(QRectF(painter.viewport())).boundingRect());
  const QRectF logicalPixmapRect(QPointF(0, 0), pixmap.deviceIndependentSize());
  const QRectF boundedTargetRect(srcRect.intersected(logicalPixmapRect));
  if (boundedTargetRect.isEmpty()) {
    return;
  }
  const qreal dpr = pixmap.devicePixelRatio();
  const QRectF boundedSourceRect(boundedTargetRect.topLeft() * dpr, boundedTargetRect.size() * dpr);
  painter.drawPixmap(boundedTargetRect, pixmap, boundedSourceRect);
}
