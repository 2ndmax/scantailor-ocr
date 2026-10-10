// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <utility>

#include "DebugImagesImpl.h"
#include "Dpm.h"
#include "Filter.h"
#include "FilterData.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "Params.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "Utils.h"
#include "filters/output/Task.h"

using namespace imageproc;

namespace page_layout {
class Task::UiUpdater : public FilterResult {
 public:
  UiUpdater(std::shared_ptr<Filter> filter,
            std::shared_ptr<Settings> settings,
            std::unique_ptr<DebugImages> dbg,
            const PageId& pageId,
            const QImage& image,
            const ImageTransformation& xform,
            const ContentMask& contentMask,
            const QRectF& adaptedContentRect,
            bool aggSizeChanged,
            bool batch);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

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
  bool m_batchProcessing;
};


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
    const QPolygonF pageRectPhys(Utils::calcPageRectPhys(data.xform(), contentRectPhys, params, aggHardSizeAfter));

    ImageTransformation newXform(data.xform());
    newXform.setPostCropArea(Utils::shiftToRoundedOrigin(newXform.transform().map(pageRectPhys)));
    return m_nextTask->process(status, FilterData(data, newXform), contentRectPhys);
  } else {
    const ContentMask contentMask(data.grayImageBlackOnWhite(), data.xform(), status);
    if (m_dbg) {
      m_dbg->add(contentMask.image(), "content_mask");
    }

    return std::make_shared<UiUpdater>(m_filter, m_settings, std::move(m_dbg), m_pageId, data.origImage(), data.xform(),
                                       contentMask, adaptedContentRect, aggHardSizeBefore != aggHardSizeAfter,
                                       m_batchProcessing);
  }
}

/*============================ Task::UiUpdater ==========================*/

Task::UiUpdater::UiUpdater(std::shared_ptr<Filter> filter,
                           std::shared_ptr<Settings> settings,
                           std::unique_ptr<DebugImages> dbg,
                           const PageId& pageId,
                           const QImage& image,
                           const ImageTransformation& xform,
                           const ContentMask& contentMask,
                           const QRectF& adaptedContentRect,
                           const bool aggSizeChanged,
                           const bool batch)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_dbg(std::move(dbg)),
      m_pageId(pageId),
      m_image(image),
      m_downscaledImage(ImageView::createDownscaledImage(image)),
      m_contentMask(contentMask),
      m_xform(xform),
      m_adaptedContentRect(adaptedContentRect),
      m_aggSizeChanged(aggSizeChanged),
      m_batchProcessing(batch) {}

void Task::UiUpdater::updateUI(FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  if (m_aggSizeChanged) {
    ui->invalidateAllThumbnails();
  } else {
    ui->invalidateThumbnail(m_pageId);
  }

  if (m_batchProcessing) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI();
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto* view = new ImageView(m_settings, m_pageId, m_image, m_downscaledImage, m_contentMask, m_xform,
                             m_adaptedContentRect, *optWidget);
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, m_dbg.get());

  QObject::connect(view, &ImageView::invalidateThumbnail, optWidget,
                   qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail));
  QObject::connect(view, &ImageView::invalidateAllThumbnails, optWidget, &OptionsWidget::invalidateAllThumbnails);
  QObject::connect(view, &ImageView::marginsSetLocally, optWidget, &OptionsWidget::marginsSetExternally);
  QObject::connect(optWidget, &OptionsWidget::marginsSetLocally, view, &ImageView::marginsSetExternally);
  QObject::connect(optWidget, &OptionsWidget::topBottomLinkToggled, view, &ImageView::topBottomLinkToggled);
  QObject::connect(optWidget, &OptionsWidget::leftRightLinkToggled, view, &ImageView::leftRightLinkToggled);
  QObject::connect(optWidget, &OptionsWidget::alignmentChanged, view, &ImageView::alignmentChanged);
  QObject::connect(optWidget, &OptionsWidget::aggregateHardSizeChanged, view, &ImageView::aggregateHardSizeChanged);
}  // Task::UiUpdater::updateUI
}  // namespace page_layout