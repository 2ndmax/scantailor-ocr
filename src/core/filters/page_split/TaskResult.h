// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_TASKRESULT_H_
#define SCANTAILOR_PAGE_SPLIT_TASKRESULT_H_

#include <QImage>
#include <memory>

#include "DebugImages.h"
#include "FilterResult.h"
#include "ImageTransformation.h"
#include "PageInfo.h"
#include "UiData.h"

class ProjectPages;

namespace page_split {
class Filter;

/**
 * A page processed by this step, for FilterUi to show.
 */
class TaskResult : public FilterResult {
 public:
  TaskResult(std::shared_ptr<Filter> filter,
             std::shared_ptr<ProjectPages> pages,
             std::unique_ptr<DebugImages> dbgImg,
             const QImage& image,
             const PageInfo& pageInfo,
             const ImageTransformation& xform,
             const UiData& uiData);

  ~TaskResult() override;

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override;

  const std::shared_ptr<ProjectPages>& pages() const { return m_pages; }
  DebugImages* dbg() const { return m_dbg.get(); }
  const QImage& image() const { return m_image; }
  const QImage& downscaledImage() const { return m_downscaledImage; }
  const PageInfo& pageInfo() const { return m_pageInfo; }
  const ImageTransformation& xform() const { return m_xform; }
  const UiData& uiData() const { return m_uiData; }

 private:
  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<ProjectPages> m_pages;
  std::unique_ptr<DebugImages> m_dbg;
  QImage m_image;
  QImage m_downscaledImage;
  PageInfo m_pageInfo;
  ImageTransformation m_xform;
  UiData m_uiData;
};
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_TASKRESULT_H_
