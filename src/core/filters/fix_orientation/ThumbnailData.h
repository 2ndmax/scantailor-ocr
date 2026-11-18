// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_THUMBNAILDATA_H_
#define SCANTAILOR_FIX_ORIENTATION_THUMBNAILDATA_H_

#include "ThumbnailDataBase.h"

namespace fix_orientation {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a ThumbnailBase.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId, const ImageTransformation& xform) : ThumbnailDataBase(imageId, xform) {}
};
}  // namespace fix_orientation
#endif  // SCANTAILOR_FIX_ORIENTATION_THUMBNAILDATA_H_
