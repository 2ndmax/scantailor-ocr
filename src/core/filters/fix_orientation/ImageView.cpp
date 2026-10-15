// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageView.h"

#include "ImagePresentation.h"
#include "Utils.h"

namespace fix_orientation {
ImageView::ImageView(const QImage& image, const QImage& downscaledImage, const ImageTransformation& xform)
    : ImageViewBase(image, downscaledImage, ImagePresentation(xform.transform(), xform.resultingPreCropArea())),
      m_dragHandler(*this),
      m_zoomHandler(*this),
      m_xform(xform) {
  rootInteractionHandler().makeLastFollower(m_dragHandler);
  rootInteractionHandler().makeLastFollower(m_zoomHandler);
}

ImageView::~ImageView() = default;

void ImageView::setRotationAndTrim(const OrthogonalRotation rotation, const ImageTrim& trim) {
  Utils::applyRotationAndTrim(m_xform, rotation, trim);

  // This should call update() by itself.
  updateTransform(ImagePresentation(m_xform.transform(), m_xform.resultingPreCropArea()));
}
}  // namespace fix_orientation
