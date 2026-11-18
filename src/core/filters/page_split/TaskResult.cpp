// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TaskResult.h"

#include <utility>

#include "DownscaledImage.h"
#include "Filter.h"
#include "FilterUiInterface.h"
#include "ProjectPages.h"

namespace page_split {
TaskResult::TaskResult(std::shared_ptr<Filter> filter,
                       std::shared_ptr<ProjectPages> pages,
                       std::unique_ptr<DebugImages> dbgImg,
                       const QImage& image,
                       const PageInfo& pageInfo,
                       const ImageTransformation& xform,
                       const UiData& uiData)
    : m_filter(std::move(filter)),
      m_pages(std::move(pages)),
      m_dbg(std::move(dbgImg)),
      m_image(image),
      m_downscaledImage(createDownscaledImage(image)),
      m_pageInfo(pageInfo),
      m_xform(xform),
      m_uiData(uiData) {}

TaskResult::~TaskResult() = default;

void TaskResult::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(m_pageInfo.id());
}

std::shared_ptr<AbstractFilter> TaskResult::filter() {
  return m_filter;
}
}  // namespace page_split
