// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGEINFO_H_
#define SCANTAILOR_CORE_IMAGEINFO_H_

#include "ImageId.h"
#include "ImageMetadata.h"

/**
 * What a project knows about one image. Used for adding images to ProjectPages objects; beyond
 * that, ProjectPages keeps its own descriptions derived from this and hands out PageInfo objects.
 */
struct ImageInfo {
  ImageInfo();

  ImageInfo(const ImageId& id,
            const ImageMetadata& metadata,
            int numSubPages,
            bool leftHalfRemoved,
            bool rightHalfRemoved);

  ImageId id;
  ImageMetadata metadata;
  int numSubPages;        // 1 or 2
  bool leftHalfRemoved;   // Both can't be true, and if one is true,
  bool rightHalfRemoved;  // then numSubPages is 1.
};


#endif  // ifndef SCANTAILOR_CORE_IMAGEINFO_H_
