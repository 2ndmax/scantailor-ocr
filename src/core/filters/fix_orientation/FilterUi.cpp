// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUi.h"

#include <utility>

#include "Filter.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "TaskResult.h"

namespace fix_orientation {
FilterUi::FilterUi(std::shared_ptr<Filter> filter) : m_filter(std::move(filter)) {}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = m_filter->optionsWidget();
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto* view = new ImageView(pageResult->image(), pageResult->downscaledImage(), pageResult->xform());
  // The rotation or the trim may have changed while the page was being processed.
  view->setRotationAndTrim(optWidget->rotation(), optWidget->trim());
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP);
  QObject::connect(optWidget, &OptionsWidget::rotationOrTrimChanged, view, &ImageView::setRotationAndTrim);
}
}  // namespace fix_orientation
