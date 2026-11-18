// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUi.h"

#include <utility>

#include "Filter.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "PageInfo.h"
#include "Settings.h"
#include "TaskResult.h"

namespace page_split {
FilterUi::FilterUi(std::shared_ptr<Filter> filter, const PageSelectionAccessor& pageSelectionAccessor)
    : m_filter(std::move(filter)), m_pageSelectionAccessor(pageSelectionAccessor) {}

void FilterUi::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  optionsWidget()->preUpdateUI(pageInfo.id());
  ui->setOptionsWidget(optionsWidget(), ui->KEEP_OWNERSHIP);
}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = optionsWidget();
  optWidget->postUpdateUI(pageResult->uiData());
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto view = new ImageView(pageResult->image(), pageResult->downscaledImage(), pageResult->xform(),
                            pageResult->uiData().pageLayout(), pageResult->pages(), pageResult->pageInfo().imageId(),
                            pageResult->pageInfo().leftHalfRemoved(), pageResult->pageInfo().rightHalfRemoved());
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, pageResult->dbg());

  QObject::connect(view, &ImageView::invalidateThumbnail, optWidget,
                   qOverload<const PageInfo&>(&OptionsWidget::invalidateThumbnail));
  QObject::connect(view, &ImageView::pageLayoutSetLocally, optWidget, &OptionsWidget::pageLayoutSetExternally);
  QObject::connect(optWidget, &OptionsWidget::pageLayoutSetLocally, view, &ImageView::pageLayoutSetExternally);
}

OptionsWidget* FilterUi::optionsWidget() {
  if (m_optionsWidget.get() == nullptr) {
    m_optionsWidget.reset(new OptionsWidget(m_filter->settings(), m_filter->pages(), m_pageSelectionAccessor));
  }
  return m_optionsWidget.get();
}
}  // namespace page_split
