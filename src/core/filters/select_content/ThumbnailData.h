// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_THUMBNAILDATA_H_
#define SCANTAILOR_SELECT_CONTENT_THUMBNAILDATA_H_

#include <QRectF>

#include "ThumbnailDataBase.h"

namespace select_content {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a Thumbnail.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId,
                const ImageTransformation& xform,
                const QRectF& contentRect,
                const QRectF& pageRect,
                bool pageRectEnabled,
                bool deviant)
      : ThumbnailDataBase(imageId, xform),
        m_contentRect(contentRect),
        m_pageRect(pageRect),
        m_pageRectEnabled(pageRectEnabled),
        m_deviant(deviant) {}

  const QRectF& contentRect() const { return m_contentRect; }

  const QRectF& pageRect() const { return m_pageRect; }

  bool pageRectEnabled() const { return m_pageRectEnabled; }

  bool deviant() const { return m_deviant; }

 private:
  QRectF m_contentRect;
  QRectF m_pageRect;
  bool m_pageRectEnabled;
  bool m_deviant;
};
}  // namespace select_content
#endif  // SCANTAILOR_SELECT_CONTENT_THUMBNAILDATA_H_
