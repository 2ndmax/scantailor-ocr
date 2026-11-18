// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_THUMBNAILDATABASE_H_
#define SCANTAILOR_CORE_THUMBNAILDATABASE_H_

#include "ImageId.h"
#include "ImageTransformation.h"

/**
 * What the thumbnail of a page shows, without widgets: the steps describe it, the user interface
 * (AbstractFilterUi::createThumbnail()) draws it. Each step describes its own in a subclass.
 */
class ThumbnailDataBase {
 public:
  ThumbnailDataBase(const ImageId& imageId, const ImageTransformation& xform) : m_imageId(imageId), m_xform(xform) {}

  virtual ~ThumbnailDataBase() = default;

  const ImageId& imageId() const { return m_imageId; }

  const ImageTransformation& xform() const { return m_xform; }

 private:
  ImageId m_imageId;
  ImageTransformation m_xform;
};

/**
 * A page the step hasn't processed with its current settings yet; drawn as an IncompleteThumbnail.
 */
class IncompleteThumbnailData : public ThumbnailDataBase {
 public:
  using ThumbnailDataBase::ThumbnailDataBase;
};

#endif  // SCANTAILOR_CORE_THUMBNAILDATABASE_H_
