// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_TASKRESULT_H_
#define SCANTAILOR_OUTPUT_TASKRESULT_H_

#include <BinaryImage.h>
#include <QImage>
#include <QRect>
#include <memory>

#include "DebugImages.h"
#include "DespeckleState.h"
#include "DespeckleVisualization.h"
#include "FilterResult.h"
#include "ImageTransformation.h"
#include "PageId.h"
#include "Params.h"

namespace output {
class Filter;
class Settings;

/**
 * A page processed by this step, for FilterUi to show.
 */
class TaskResult : public FilterResult {
 public:
  TaskResult(std::shared_ptr<Filter> filter,
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
             bool debug);

  ~TaskResult() override;

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override;

  const std::shared_ptr<Settings>& settings() const { return m_settings; }
  DebugImages* dbg() const { return m_dbg.get(); }
  const Params& params() const { return m_params; }
  const ImageTransformation& xform() const { return m_xform; }
  const QRect& virtContentRect() const { return m_virtContentRect; }
  const PageId& pageId() const { return m_pageId; }
  const QImage& origImage() const { return m_origImage; }
  const QImage& downscaledOrigImage() const { return m_downscaledOrigImage; }
  const QImage& outputImage() const { return m_outputImage; }
  const QImage& downscaledOutputImage() const { return m_downscaledOutputImage; }
  const imageproc::BinaryImage& pictureMask() const { return m_pictureMask; }
  const DespeckleState& despeckleState() const { return m_despeckleState; }
  const DespeckleVisualization& despeckleVisualization() const { return m_despeckleVisualization; }
  bool debug() const { return m_debug; }

 private:
  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<Settings> m_settings;
  std::unique_ptr<DebugImages> m_dbg;
  Params m_params;
  ImageTransformation m_xform;
  QRect m_virtContentRect;
  PageId m_pageId;
  QImage m_origImage;
  QImage m_downscaledOrigImage;
  QImage m_outputImage;
  QImage m_downscaledOutputImage;
  imageproc::BinaryImage m_pictureMask;
  DespeckleState m_despeckleState;
  DespeckleVisualization m_despeckleVisualization;
  bool m_debug;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_TASKRESULT_H_
