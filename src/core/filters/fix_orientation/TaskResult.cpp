// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TaskResult.h"

#include <utility>

#include "DownscaledImage.h"
#include "Filter.h"
#include "FilterUiInterface.h"

namespace fix_orientation {
TaskResult::TaskResult(std::shared_ptr<Filter> filter,
                       const QImage& image,
                       const ImageId& imageId,
                       const ImageTransformation& xform)
    : m_filter(std::move(filter)),
      m_image(image),
      m_downscaledImage(createDownscaledImage(image)),
      m_imageId(imageId),
      m_xform(xform) {}

void TaskResult::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(PageId(m_imageId));
}

std::shared_ptr<AbstractFilter> TaskResult::filter() {
  return m_filter;
}
}  // namespace fix_orientation
