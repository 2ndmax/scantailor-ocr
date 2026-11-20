// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Jp2MetadataLoader.h"

#include "Jp2Reader.h"

ImageMetadataLoader::Status Jp2MetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                            const VirtualFunction<void, const ImageMetadata&>& out) {
  return Jp2Reader::readMetadata(ioDevice, out);
}
