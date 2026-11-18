// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUiSequence.h"

#include <utility>

#include "AbstractFilterUi.h"
#include "FilterResult.h"
#include "StageSequence.h"
#include "filters/deskew/FilterUi.h"
#include "filters/fix_orientation/FilterUi.h"
#include "filters/output/FilterUi.h"
#include "filters/page_layout/FilterUi.h"
#include "filters/page_split/FilterUi.h"
#include "filters/select_content/FilterUi.h"

FilterUiSequence::FilterUiSequence(std::shared_ptr<StageSequence> stages)
    : m_stages(std::move(stages)), m_filterUis(m_stages->count()) {
  m_filterUis[m_stages->fixOrientationFilterIdx()]
      = std::make_unique<fix_orientation::FilterUi>(m_stages->fixOrientationFilter());
  m_filterUis[m_stages->pageSplitFilterIdx()] = std::make_unique<page_split::FilterUi>(m_stages->pageSplitFilter());
  m_filterUis[m_stages->deskewFilterIdx()] = std::make_unique<deskew::FilterUi>(m_stages->deskewFilter());
  m_filterUis[m_stages->selectContentFilterIdx()]
      = std::make_unique<select_content::FilterUi>(m_stages->selectContentFilter());
  m_filterUis[m_stages->pageLayoutFilterIdx()] = std::make_unique<page_layout::FilterUi>(m_stages->pageLayoutFilter());
  m_filterUis[m_stages->outputFilterIdx()] = std::make_unique<output::FilterUi>(m_stages->outputFilter());
}

FilterUiSequence::~FilterUiSequence() = default;

void FilterUiSequence::showResult(FilterResult& result, FilterUiInterface* ui) const {
  const std::shared_ptr<AbstractFilter> filter = result.filter();
  if (!filter) {
    return;
  }
  const int filterIdx = m_stages->findFilter(filter);
  if ((filterIdx >= 0) && m_filterUis[filterIdx]) {
    m_filterUis[filterIdx]->showResult(result, ui);
  }
}
