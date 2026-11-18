// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_TASKRESULT_H_
#define SCANTAILOR_SELECT_CONTENT_TASKRESULT_H_

#include <QImage>
#include <memory>

#include "ContentMask.h"
#include "DebugImages.h"
#include "FilterResult.h"
#include "ImageTransformation.h"
#include "PageId.h"
#include "UiData.h"

namespace select_content {
class Filter;

/**
 * A page processed by this step, for FilterUi to show.
 */
class TaskResult : public FilterResult {
 public:
  TaskResult(std::shared_ptr<Filter> filter,
             const PageId& pageId,
             std::unique_ptr<DebugImages> dbg,
             const QImage& image,
             const ImageTransformation& xform,
             const ContentMask& contentMask,
             const UiData& uiData);

  ~TaskResult() override;

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override;

  const PageId& pageId() const { return m_pageId; }
  DebugImages* dbg() const { return m_dbg.get(); }
  const QImage& image() const { return m_image; }
  const QImage& downscaledImage() const { return m_downscaledImage; }
  const ContentMask& contentMask() const { return m_contentMask; }
  const ImageTransformation& xform() const { return m_xform; }
  const UiData& uiData() const { return m_uiData; }

 private:
  std::shared_ptr<Filter> m_filter;
  PageId m_pageId;
  std::unique_ptr<DebugImages> m_dbg;
  QImage m_image;
  QImage m_downscaledImage;
  ContentMask m_contentMask;
  ImageTransformation m_xform;
  UiData m_uiData;
};
}  // namespace select_content
#endif  // ifndef SCANTAILOR_SELECT_CONTENT_TASKRESULT_H_
