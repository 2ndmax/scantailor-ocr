// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_TASKRESULT_H_
#define SCANTAILOR_PAGE_LAYOUT_TASKRESULT_H_

#include <QImage>
#include <QRectF>
#include <memory>

#include "ContentMask.h"
#include "DebugImages.h"
#include "FilterResult.h"
#include "ImageTransformation.h"
#include "PageId.h"

namespace page_layout {
class Filter;
class Settings;

/**
 * A page processed by this step, for FilterUi to show.
 */
class TaskResult : public FilterResult {
 public:
  TaskResult(std::shared_ptr<Filter> filter,
             std::shared_ptr<Settings> settings,
             std::unique_ptr<DebugImages> dbg,
             const PageId& pageId,
             const QImage& image,
             const ImageTransformation& xform,
             const ContentMask& contentMask,
             const QRectF& adaptedContentRect,
             bool aggSizeChanged);

  ~TaskResult() override;

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override;

  const std::shared_ptr<Settings>& settings() const { return m_settings; }
  DebugImages* dbg() const { return m_dbg.get(); }
  const PageId& pageId() const { return m_pageId; }
  const QImage& image() const { return m_image; }
  const QImage& downscaledImage() const { return m_downscaledImage; }
  const ContentMask& contentMask() const { return m_contentMask; }
  const ImageTransformation& xform() const { return m_xform; }
  const QRectF& adaptedContentRect() const { return m_adaptedContentRect; }
  bool aggSizeChanged() const { return m_aggSizeChanged; }

 private:
  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<Settings> m_settings;
  std::unique_ptr<DebugImages> m_dbg;
  PageId m_pageId;
  QImage m_image;
  QImage m_downscaledImage;
  ContentMask m_contentMask;
  ImageTransformation m_xform;
  QRectF m_adaptedContentRect;
  bool m_aggSizeChanged;
};
}  // namespace page_layout
#endif  // ifndef SCANTAILOR_PAGE_LAYOUT_TASKRESULT_H_
