// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_THUMBNAIL_H_
#define SCANTAILOR_PAGE_SPLIT_THUMBNAIL_H_

#include <QPixmap>
#include <memory>

#include "../PageLayout.h"
#include "ThumbnailBase.h"

class QSizeF;
class ThumbnailPixmapCache;
class ImageId;
class ImageTransformation;

namespace page_split {
class Thumbnail : public ThumbnailBase {
 public:
  Thumbnail(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
            const QSizeF& maxSize,
            const ImageId& imageId,
            const ImageTransformation& xform,
            const PageLayout& layout,
            bool leftHalfRemoved,
            bool rightHalfRemoved);

  void prePaintOverImage(QPainter& painter,
                         const QTransform& imageToDisplay,
                         const QTransform& thumbToDisplay) override;

 private:
  PageLayout m_layout;
  QPixmap m_trashPixmap;
  bool m_leftHalfRemoved;
  bool m_rightHalfRemoved;
};
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_THUMBNAIL_H_
