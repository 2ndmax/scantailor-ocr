// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"
#include "TaskResult.h"

#include <utility>

#include "BatchProcessingResult.h"
#include "DebugImagesImpl.h"
#include "Dpm.h"
#include "Filter.h"
#include "FilterData.h"
#include "Params.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "Utils.h"
#include "../output/Task.h"

using namespace imageproc;

namespace page_layout {
Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<output::Task> nextTask,
           std::shared_ptr<Settings> settings,
           const PageId& pageId,
           bool batch,
           bool debug)
    : m_filter(std::move(filter)),
      m_nextTask(std::move(nextTask)),
      m_settings(std::move(settings)),
      m_pageId(pageId),
      m_batchProcessing(batch) {
  if (debug) {
    m_dbg = std::make_unique<DebugImagesImpl>();
  }
}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status,
                              const FilterData& data,
                              const QRectF& pageRect,
                              const QRectF& contentRect) {
  status.throwIfCancelled();

  const QSizeF contentSizeMm(Utils::calcRectSizeMM(data.xform(), contentRect));

  if (m_settings->isPageAutoMarginsEnabled(m_pageId)) {
    const Margins& marginsMm = Utils::calcMarginsMM(data.xform(), pageRect, contentRect);
    m_settings->setHardMarginsMM(m_pageId, marginsMm);
  }

  QSizeF aggHardSizeBefore;
  QSizeF aggHardSizeAfter;
  const Params params(m_settings->updateContentSizeAndGetParams(m_pageId, pageRect, contentRect, contentSizeMm,
                                                                &aggHardSizeBefore, &aggHardSizeAfter));

  const QRectF adaptedContentRect(Utils::adaptContentRect(data.xform(), contentRect));

  if (m_nextTask) {
    const QPolygonF contentRectPhys(data.xform().transformBack().map(adaptedContentRect));
    double scaleFactor = 1.0;
    const QPolygonF pageRectPhys(Utils::calcPageRectPhys(data.xform(), contentRectPhys, params, aggHardSizeAfter,
                                                         m_settings->outputSizeOptions(), &scaleFactor));

    ImageTransformation newXform(data.xform());
    newXform.setPostCropArea(Utils::shiftToRoundedOrigin(newXform.transform().map(pageRectPhys)));
    if (scaleFactor != 1.0) {
      newXform.setPostScaleFactor(scaleFactor);
    }
    return m_nextTask->process(status, FilterData(data, newXform), contentRectPhys);
  } else if (m_batchProcessing) {
    return std::make_shared<BatchProcessingResult>(m_filter, m_pageId,
                                                   aggHardSizeBefore != aggHardSizeAfter
                                                       ? BatchProcessingResult::ALL_THUMBNAILS
                                                       : BatchProcessingResult::PAGE_THUMBNAIL);
  } else {
    const ContentMask contentMask(data.grayImageBlackOnWhite(), data.xform(), status);
    if (m_dbg) {
      m_dbg->add(contentMask.image(), "content_mask");
    }

    return std::make_shared<TaskResult>(m_filter, m_settings, std::move(m_dbg), m_pageId, data.origImage(),
                                        data.xform(), contentMask, adaptedContentRect,
                                        aggHardSizeBefore != aggHardSizeAfter);
  }
}
}  // namespace page_layout
