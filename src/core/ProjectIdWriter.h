// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROJECTIDWRITER_H_
#define SCANTAILOR_CORE_PROJECTIDWRITER_H_

#include "ImageId.h"
#include "PageId.h"
#include "VirtualFunction.h"

class QDomDocument;
class QDomElement;

/**
 * The small part of a project writer that the processing steps need: enumerating
 * images and pages with their numeric ids. The full writer (ProjectWriter) lives
 * in the composition layer and implements this.
 */
class ProjectIdWriter {
 public:
  virtual ~ProjectIdWriter() = default;

  /**
   * \p out will be called like this: out(ImageId, numeric_image_id)
   */
  template <typename OutFunc>
  void enumImages(OutFunc out) const {
    enumImagesImpl(ProxyFunction<OutFunc, void, const ImageId&, int>(out));
  }

  /**
   * \p out will be called like this: out(LogicalPageId, numeric_page_id)
   */
  template <typename OutFunc>
  void enumPages(OutFunc out) const {
    enumPagesImpl(ProxyFunction<OutFunc, void, const PageId&, int>(out));
  }

 protected:
  virtual void enumImagesImpl(const VirtualFunction<void, const ImageId&, int>& out) const = 0;

  virtual void enumPagesImpl(const VirtualFunction<void, const PageId&, int>& out) const = 0;
};

#endif  // ifndef SCANTAILOR_CORE_PROJECTIDWRITER_H_