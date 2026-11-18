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

namespace deskew {
FilterUi::FilterUi(std::shared_ptr<Filter> filter) : m_filter(std::move(filter)) {}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  optWidget->postUpdateUI(pageResult->uiData());
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto* view = new ImageView(pageResult->image(), pageResult->downscaledImage(), pageResult->xform());
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, pageResult->dbg());

  QObject::connect(view, &ImageView::manualDeskewAngleSet, optWidget, &OptionsWidget::manualDeskewAngleSetExternally);
  QObject::connect(optWidget, &OptionsWidget::manualDeskewAngleSet, view, &ImageView::manualDeskewAngleSetExternally);
  QObject::connect(view, &ImageView::manualObliqueAngleSet, optWidget, &OptionsWidget::manualObliqueAngleSetExternally);
  QObject::connect(optWidget, &OptionsWidget::manualObliqueAngleSet, view, &ImageView::manualObliqueAngleSetExternally);
}
}  // namespace deskew
