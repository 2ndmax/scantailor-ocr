// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "IncompleteThumbnail.h"

#include <utility>

IncompleteThumbnail::IncompleteThumbnail(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                                         const QSizeF& maxSize,
                                         const ImageId& imageId,
                                         const ImageTransformation& imageXform)
    : ThumbnailBase(std::move(thumbnailCache), maxSize, imageId, imageXform) {
  addMark(this, INCOMPLETE_MARK);
}

IncompleteThumbnail::~IncompleteThumbnail() = default;
