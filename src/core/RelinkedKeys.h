// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_RELINKEDKEYS_H_
#define SCANTAILOR_CORE_RELINKEDKEYS_H_

#include "AbstractRelinker.h"
#include "ImageId.h"
#include "PageId.h"
#include "RelinkablePath.h"

/** The image id with the file path that the relinker substitutes for its file. */
inline ImageId relinkedKey(const ImageId& imageId, const AbstractRelinker& relinker) {
  ImageId newImageId(imageId);
  newImageId.setFilePath(relinker.substitutionPathFor(RelinkablePath(imageId.filePath(), RelinkablePath::File)));
  return newImageId;
}

/** The page id with the file path that the relinker substitutes for its image file. */
inline PageId relinkedKey(const PageId& pageId, const AbstractRelinker& relinker) {
  PageId newPageId(pageId);
  newPageId.imageId() = relinkedKey(pageId.imageId(), relinker);
  return newPageId;
}

/**
 * The values of a map keyed by ImageId or PageId, under the keys that the relinker substitutes.
 *
 * REVIEW-NOTE (2026-10): two keys relinked onto the same file -> only the first one in the map's
 * order keeps its value, the other page or image loses its settings. Not fixed because such a
 * project already contains the same page twice.
 */
template <typename Map>
Map relinkedKeys(const Map& map, const AbstractRelinker& relinker) {
  Map result;
  for (const auto& [key, value] : map) {
    result.emplace(relinkedKey(key, relinker), value);
  }
  return result;
}

#endif  // SCANTAILOR_CORE_RELINKEDKEYS_H_
