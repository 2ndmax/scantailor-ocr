// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TaskResult.h"

#include <utility>

#include "DownscaledImage.h"
#include "Filter.h"
#include "FilterUiInterface.h"
#include "Settings.h"

namespace page_layout {
TaskResult::TaskResult(std::shared_ptr<Filter> filter,
                       std::shared_ptr<Settings> settings,
                       std::unique_ptr<DebugImages> dbg,
                       const PageId& pageId,
                       const QImage& image,
                       const ImageTransformation& xform,
                       const ContentMask& contentMask,
                       const QRectF& adaptedContentRect,
                       const bool aggSizeChanged)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_dbg(std::move(dbg)),
      m_pageId(pageId),
      m_image(image),
      m_downscaledImage(createDownscaledImage(image)),
      m_contentMask(contentMask),
      m_xform(xform),
      m_adaptedContentRect(adaptedContentRect),
      m_aggSizeChanged(aggSizeChanged) {}

TaskResult::~TaskResult() = default;

void TaskResult::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  if (m_aggSizeChanged) {
    ui->invalidateAllThumbnails();
  } else {
    ui->invalidateThumbnail(m_pageId);
  }
}

std::shared_ptr<AbstractFilter> TaskResult::filter() {
  return m_filter;
}
}  // namespace page_layout
