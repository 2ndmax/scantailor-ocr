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

namespace deskew {
FilterUi::FilterUi(std::shared_ptr<Filter> filter, const PageSelectionAccessor& pageSelectionAccessor)
    : m_filter(std::move(filter)), m_pageSelectionAccessor(pageSelectionAccessor) {}

FilterUi::~FilterUi() = default;

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

  auto* view = new ImageView(pageResult->image(), pageResult->downscaledImage(), pageResult->xform());
  ui->setImageWidget(view, ui->TRANSFER_OWNERSHIP, pageResult->dbg());

  QObject::connect(view, &ImageView::manualDeskewAngleSet, optWidget, &OptionsWidget::manualDeskewAngleSetExternally);
  QObject::connect(optWidget, &OptionsWidget::manualDeskewAngleSet, view, &ImageView::manualDeskewAngleSetExternally);
  QObject::connect(view, &ImageView::manualObliqueAngleSet, optWidget, &OptionsWidget::manualObliqueAngleSetExternally);
  QObject::connect(optWidget, &OptionsWidget::manualObliqueAngleSet, view, &ImageView::manualObliqueAngleSetExternally);
}

OptionsWidget* FilterUi::optionsWidget() {
  if (m_optionsWidget.get() == nullptr) {
    m_optionsWidget.reset(new OptionsWidget(m_filter->settings(), m_filter->defaultParams(), m_pageSelectionAccessor));
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
  return std::make_unique<Thumbnail>(std::move(cache), maxSize, stepData->imageId(), stepData->xform(),
                                     stepData->deviant());
}
}  // namespace deskew
