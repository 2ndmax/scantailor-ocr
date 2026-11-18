// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TaskResult.h"

#include <utility>

#include "DownscaledImage.h"
#include "Filter.h"
#include "FilterUiInterface.h"
#include "Settings.h"

namespace output {
TaskResult::TaskResult(std::shared_ptr<Filter> filter,
                       std::shared_ptr<Settings> settings,
                       std::unique_ptr<DebugImages> dbgImg,
                       const Params& params,
                       const ImageTransformation& xform,
                       const QRect& virtContentRect,
                       const PageId& pageId,
                       const QImage& origImage,
                       const QImage& outputImage,
                       const imageproc::BinaryImage& pictureMask,
                       const DespeckleState& despeckleState,
                       const DespeckleVisualization& despeckleVisualization,
                       const bool debug)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_dbg(std::move(dbgImg)),
      m_params(params),
      m_xform(xform),
      m_virtContentRect(virtContentRect),
      m_pageId(pageId),
      m_origImage(origImage),
      m_downscaledOrigImage(createDownscaledImage(origImage)),
      m_outputImage(outputImage),
      m_downscaledOutputImage(createDownscaledImage(outputImage)),
      m_pictureMask(pictureMask),
      m_despeckleState(despeckleState),
      m_despeckleVisualization(despeckleVisualization),
      m_debug(debug) {}

TaskResult::~TaskResult() = default;

void TaskResult::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  ui->invalidateThumbnail(m_pageId);
}

std::shared_ptr<AbstractFilter> TaskResult::filter() {
  return m_filter;
}
}  // namespace output
