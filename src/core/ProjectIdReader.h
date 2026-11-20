// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROJECTIDREADER_H_
#define SCANTAILOR_CORE_PROJECTIDREADER_H_

#include "ImageId.h"
#include "PageId.h"

/**
 * The small part of a project reader that the processing steps need: mapping
 * numeric ids from the project file back to ImageId/PageId. The full reader
 * (ProjectReader) lives in the composition layer and implements this.
 */
class ProjectIdReader {
 public:
  virtual ~ProjectIdReader() = default;

  virtual ImageId imageId(int numericId) const = 0;

  virtual PageId pageId(int numericId) const = 0;
};

#endif  // ifndef SCANTAILOR_CORE_PROJECTIDREADER_H_