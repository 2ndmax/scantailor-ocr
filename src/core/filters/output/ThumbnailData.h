// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_THUMBNAILDATA_H_
#define SCANTAILOR_OUTPUT_THUMBNAILDATA_H_

#include "ThumbnailDataBase.h"

namespace output {
/**
 * What the thumbnail of a page shows in this step; FilterUi draws it as a Thumbnail.
 */
class ThumbnailData : public ThumbnailDataBase {
 public:
  ThumbnailData(const ImageId& imageId, const ImageTransformation& xform) : ThumbnailDataBase(imageId, xform) {}
};
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_THUMBNAILDATA_H_
