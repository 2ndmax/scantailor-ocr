// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageFileInfo.h"


bool ImageFileInfo::isDpiOK() const {
  return std::find_if(m_imageInfo.begin(), m_imageInfo.end(),
                      [](const ImageMetadata& metadata) { return !metadata.isDpiOK(); })
         == m_imageInfo.end();
}
