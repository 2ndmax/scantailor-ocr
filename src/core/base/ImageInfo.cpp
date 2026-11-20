// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageInfo.h"

ImageInfo::ImageInfo() : numSubPages(0), leftHalfRemoved(false), rightHalfRemoved(false) {}

ImageInfo::ImageInfo(const ImageId& id,
                     const ImageMetadata& metadata,
                     int numSubPages,
                     bool leftHalfRemoved,
                     bool rightHalfRemoved)
    : id(id),
      metadata(metadata),
      numSubPages(numSubPages),
      leftHalfRemoved(leftHalfRemoved),
      rightHalfRemoved(rightHalfRemoved) {}
