// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUiSequence.h"

#include <utility>

#include "AbstractCommand.h"
#include "AbstractFilterUi.h"
#include "ErrorWidget.h"
#include "FilterOptionsWidget.h"
#include "FilterResult.h"
#include "FilterUiInterface.h"
#include "IncompleteThumbnail.h"
#include "LoadFileTask.h"
#include "PageSelectionAccessor.h"
#include "StageSequence.h"
#include "ThumbnailDataBase.h"
#include "filters/deskew/ui/FilterUi.h"
#include "filters/fix_orientation/ui/FilterUi.h"
#include "filters/output/ui/FilterUi.h"
#include "filters/page_layout/ui/FilterUi.h"
#include "filters/page_split/ui/FilterUi.h"
#include "filters/select_content/ui/FilterUi.h"

namespace {
class ErrWidget : public ErrorWidget {
 public:
  ErrWidget(std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester,
            const QString& text,
            Qt::TextFormat fmt = Qt::AutoText)
      : ErrorWidget(text, fmt), m_relinkingDialogRequester(std::move(relinkingDialogRequester)) {}

 private:
  void linkActivated(const QString& /*link*/) override { (*m_relinkingDialogRequester)(); }

  std::shared_ptr<AbstractCommand<void>> m_relinkingDialogRequester;
};

// A page that couldn't be loaded or processed: its message in place of the page, and no options.
void showLoadError(const FilterResult& result, FilterUiInterface* ui) {
  QString message;
  Qt::TextFormat format = Qt::AutoText;
  if (!LoadFileTask::errorMessage(result, &message, &format)) {
    return;
  }
  ui->setImageWidget(new ErrWidget(ui->relinkingDialogRequester(), message, format), ui->TRANSFER_OWNERSHIP);
  ui->setOptionsWidget(new FilterOptionsWidget, ui->TRANSFER_OWNERSHIP);
}
}  // namespace

FilterUiSequence::FilterUiSequence(std::shared_ptr<StageSequence> stages,
                                   const PageSelectionAccessor& pageSelectionAccessor)
    : m_stages(std::move(stages)), m_filterUis(m_stages->count()) {
  m_filterUis[m_stages->fixOrientationFilterIdx()]
      = std::make_unique<fix_orientation::FilterUi>(m_stages->fixOrientationFilter(), pageSelectionAccessor);
  m_filterUis[m_stages->pageSplitFilterIdx()]
      = std::make_unique<page_split::FilterUi>(m_stages->pageSplitFilter(), pageSelectionAccessor);
  m_filterUis[m_stages->deskewFilterIdx()]
      = std::make_unique<deskew::FilterUi>(m_stages->deskewFilter(), pageSelectionAccessor);
  m_filterUis[m_stages->selectContentFilterIdx()]
      = std::make_unique<select_content::FilterUi>(m_stages->selectContentFilter(), pageSelectionAccessor);
  m_filterUis[m_stages->pageLayoutFilterIdx()]
      = std::make_unique<page_layout::FilterUi>(m_stages->pageLayoutFilter(), pageSelectionAccessor);
  m_filterUis[m_stages->outputFilterIdx()]
      = std::make_unique<output::FilterUi>(m_stages->outputFilter(), pageSelectionAccessor);
}

FilterUiSequence::~FilterUiSequence() = default;

void FilterUiSequence::preUpdateUI(const int filterIdx, FilterUiInterface* ui, const PageInfo& pageInfo) const {
  m_filterUis[filterIdx]->preUpdateUI(ui, pageInfo);
}

void FilterUiSequence::showResult(FilterResult& result, FilterUiInterface* ui) const {
  const std::shared_ptr<AbstractFilter> filter = result.filter();
  // A result of no step is a page that could not be loaded or processed.
  if (!filter) {
    showLoadError(result, ui);
    return;
  }
  const int filterIdx = m_stages->findFilter(filter);
  if (filterIdx >= 0) {
    m_filterUis[filterIdx]->showResult(result, ui);
  }
}

std::unique_ptr<QGraphicsItem> FilterUiSequence::createThumbnail(const ThumbnailDataBase& data,
                                                                 const std::shared_ptr<ThumbnailPixmapCache>& cache,
                                                                 const QSizeF& maxSize) const {
  // A page a step hasn't processed yet looks the same in every step.
  if (dynamic_cast<const IncompleteThumbnailData*>(&data)) {
    return std::make_unique<IncompleteThumbnail>(cache, maxSize, data.imageId(), data.xform());
  }
  for (const std::unique_ptr<AbstractFilterUi>& filterUi : m_filterUis) {
    if (std::unique_ptr<QGraphicsItem> thumbnail = filterUi->createThumbnail(data, cache, maxSize)) {
      return thumbnail;
    }
  }
  return nullptr;
}
