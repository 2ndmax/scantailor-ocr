// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_THUMBNAILDATA_H_
#define SCANTAILOR_PAGE_SPLIT_THUMBNAILDATA_H_

#include "PageLayout.h"
#include "ThumbnailDataBase.h"

namespace page_split {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a Thumbnail.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId,
                const ImageTransformation& xform,
                const PageLayout& layout,
                bool leftHalfRemoved,
                bool rightHalfRemoved)
      : ThumbnailDataBase(imageId, xform),
        m_layout(layout),
        m_leftHalfRemoved(leftHalfRemoved),
        m_rightHalfRemoved(rightHalfRemoved) {}

  const PageLayout& layout() const { return m_layout; }

  bool leftHalfRemoved() const { return m_leftHalfRemoved; }

  bool rightHalfRemoved() const { return m_rightHalfRemoved; }

 private:
  PageLayout m_layout;
  bool m_leftHalfRemoved;
  bool m_rightHalfRemoved;
};
}  // namespace page_split
#endif  // SCANTAILOR_PAGE_SPLIT_THUMBNAILDATA_H_
