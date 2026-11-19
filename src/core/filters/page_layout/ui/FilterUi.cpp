// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUi.h"

#include <utility>

#include "../Filter.h"
#include "../Settings.h"
#include "../TaskResult.h"
#include "../ThumbnailData.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "PageInfo.h"
#include "Thumbnail.h"

using namespace imageproc;

namespace page_layout {
FilterUi::FilterUi(std::shared_ptr<Filter> filter, const PageSelectionAccessor& pageSelectionAccessor)
    : m_filter(std::move(filter)), m_pageSelectionAccessor(pageSelectionAccessor) {}

FilterUi::~FilterUi() = default;

void FilterUi::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  const Margins marginsMm(m_filter->settings()->getHardMarginsMM(pageInfo.id()));
  const Alignment alignment(m_filter->settings()->getPageAlignment(pageInfo.id()));
  optionsWidget()->preUpdateUI(pageInfo, marginsMm, alignment);
  ui->setOptionsWidget(optionsWidget(), ui->KEEP_OWNERSHIP);
}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = optionsWidget();
  optWidget->postUpdateUI();
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto* view
      = new ImageView(pageResult->settings(), pageResult->pageId(), pageResult->image(), pageResult->downscaledImage(),
                      pageResult->contentMask(), pageResult->xform(), pageResult->adaptedContentRect(), *optWidget);
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, pageResult->dbg());

  QObject::connect(view, &ImageView::invalidateThumbnail, optWidget,
                   qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail));
  QObject::connect(view, &ImageView::invalidateAllThumbnails, optWidget, &OptionsWidget::invalidateAllThumbnails);
  QObject::connect(view, &ImageView::marginsSetLocally, optWidget, &OptionsWidget::marginsSetExternally);
  QObject::connect(optWidget, &OptionsWidget::marginsSetLocally, view, &ImageView::marginsSetExternally);
  QObject::connect(optWidget, &OptionsWidget::topBottomLinkToggled, view, &ImageView::topBottomLinkToggled);
  QObject::connect(optWidget, &OptionsWidget::leftRightLinkToggled, view, &ImageView::leftRightLinkToggled);
  QObject::connect(optWidget, &OptionsWidget::alignmentChanged, view, &ImageView::alignmentChanged);
  QObject::connect(optWidget, &OptionsWidget::aggregateHardSizeChanged, view, &ImageView::aggregateHardSizeChanged);
}

OptionsWidget* FilterUi::optionsWidget() {
  if (m_optionsWidget.get() == nullptr) {
    m_optionsWidget.reset(new OptionsWidget(m_filter->settings(), m_filter->pages(), m_pageSelectionAccessor));
  }
  return m_optionsWidget.get();
}

std::unique_ptr<QGraphicsItem> FilterUi::createThumbnail(const ThumbnailDataBase& data,
                                                         std::shared_ptr<ThumbnailPixmapCache> cache,
                                                         const QSizeF& maxSize) const {
  const auto* stepData = dynamic_cast<const ThumbnailData*>(&data);
  if (!stepData) {
    return nullptr;
  }
  return std::make_unique<Thumbnail>(std::move(cache), maxSize, stepData->imageId(), stepData->params(),
                                     stepData->xform(), stepData->physContentRect(), stepData->deviant());
}
}  // namespace page_layout
