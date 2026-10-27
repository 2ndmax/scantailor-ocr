// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Thumbnail.h"

#include <OverlayStyle.h>

#include <QPainter>
#include <utility>

namespace select_content {
Thumbnail::Thumbnail(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                     const QSizeF& maxSize,
                     const ImageId& imageId,
                     const ImageTransformation& xform,
                     const QRectF& contentRect,
                     const QRectF& pageRect,
                     bool pageRectEnabled,
                     bool deviant)
    : ThumbnailBase(std::move(thumbnailCache), maxSize, imageId, xform),
      m_contentRect(contentRect),
      m_pageRect(pageRect),
      m_pageRectEnabled(pageRectEnabled) {
  if (deviant) {
    addMark(this, DEVIANT_MARK);
  }
}

void Thumbnail::paintOverImage(QPainter& painter, const QTransform& imageToDisplay, const QTransform& thumbToDisplay) {
  const QRectF pageRect(virtToThumb().mapRect(m_pageRect));
  painter.setRenderHint(QPainter::Antialiasing, false);

  // The page box is shown also on pages without content, as in the image view.
  if (m_pageRectEnabled && !m_pageRect.isNull()) {
    painter.setPen(OverlayStyle::pen(OverlayStyle::secondary(), OverlayStyle::Line::Thumbnail));

    painter.setBrush(Qt::NoBrush);

    painter.drawRect(pageRect);
  }

  if (!m_contentRect.isNull()) {
    painter.setPen(OverlayStyle::pen(OverlayStyle::primary(), OverlayStyle::Line::Thumbnail));

    painter.setBrush(OverlayStyle::primary(50));

    QRectF contentRect(virtToThumb().mapRect(m_contentRect));

    // Adjust to compensate for pen width.
    contentRect.adjust(-1, -1, 1, 1);
    if (!m_pageRect.isNull()) {
      // Without a page box (an old project file) the content box is not cut.
      contentRect = contentRect.intersected(pageRect);
    }

    painter.drawRect(contentRect);
  }
}
}  // namespace select_content