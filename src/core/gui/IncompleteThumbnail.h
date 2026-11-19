// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_INCOMPLETETHUMBNAIL_H_
#define SCANTAILOR_CORE_INCOMPLETETHUMBNAIL_H_

#include <memory>

#include "ThumbnailBase.h"

class ThumbnailPixmapCache;
class QSizeF;
class QRectF;

/**
 * \brief A thumbnail represeting a page not completely processed.
 *
 * Suppose you have switched to the Deskew filter without running all images
 * through the previous filters. The thumbnail view of the Deskew filter
 * is supposed to show already split and deskewed pages. However, because
 * the previous filters were not applied to all pages, we can't show them
 * split and deskewed. In that case, we show an image the best we can
 * (maybe split but not deskewed, maybe unsplit). Also we draw an hourglass at the
 * top right corner of that image to indicate it's not shown the way it should.
 */
class IncompleteThumbnail : public ThumbnailBase {
 public:
  IncompleteThumbnail(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                      const QSizeF& maxSize,
                      const ImageId& imageId,
                      const ImageTransformation& imageXform);

  ~IncompleteThumbnail() override;
};


#endif
