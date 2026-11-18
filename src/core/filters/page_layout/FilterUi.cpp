// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUi.h"

#include <utility>

#include "Filter.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "TaskResult.h"

using namespace imageproc;

namespace page_layout {
FilterUi::FilterUi(std::shared_ptr<Filter> filter) : m_filter(std::move(filter)) {}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
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
}  // namespace page_layout
