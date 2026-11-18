// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_THUMBNAILDATA_H_
#define SCANTAILOR_PAGE_LAYOUT_THUMBNAILDATA_H_

#include <QPolygonF>

#include "Params.h"
#include "ThumbnailDataBase.h"

namespace page_layout {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a Thumbnail.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId,
                const Params& params,
                const ImageTransformation& xform,
                const QPolygonF& physContentRect,
                bool deviant)
      : ThumbnailDataBase(imageId, xform), m_params(params), m_physContentRect(physContentRect), m_deviant(deviant) {}

  const Params& params() const { return m_params; }

  const QPolygonF& physContentRect() const { return m_physContentRect; }

  bool deviant() const { return m_deviant; }

 private:
  Params m_params;
  QPolygonF m_physContentRect;
  bool m_deviant;
};
}  // namespace page_layout
#endif  // SCANTAILOR_PAGE_LAYOUT_THUMBNAILDATA_H_
