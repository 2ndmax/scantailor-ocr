// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_TASKRESULT_H_
#define SCANTAILOR_FIX_ORIENTATION_TASKRESULT_H_

#include <QImage>
#include <memory>

#include "FilterResult.h"
#include "ImageId.h"
#include "ImageTransformation.h"

namespace fix_orientation {
class Filter;

/**
 * A page processed by this step, for FilterUi to show.
 */
class TaskResult : public FilterResult {
 public:
  TaskResult(std::shared_ptr<Filter> filter,
             const QImage& image,
             const ImageId& imageId,
             const ImageTransformation& xform);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override;

  const QImage& image() const { return m_image; }

  const QImage& downscaledImage() const { return m_downscaledImage; }

  const ImageTransformation& xform() const { return m_xform; }

 private:
  std::shared_ptr<Filter> m_filter;
  QImage m_image;
  QImage m_downscaledImage;
  ImageId m_imageId;
  ImageTransformation m_xform;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_TASKRESULT_H_
