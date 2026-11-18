// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_THUMBNAILDATA_H_
#define SCANTAILOR_DESKEW_THUMBNAILDATA_H_

#include "ThumbnailDataBase.h"

namespace deskew {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a Thumbnail.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId, const ImageTransformation& xform, bool deviant)
      : ThumbnailDataBase(imageId, xform), m_deviant(deviant) {}

  bool deviant() const { return m_deviant; }

 private:
  bool m_deviant;
};
}  // namespace deskew
#endif  // SCANTAILOR_DESKEW_THUMBNAILDATA_H_
