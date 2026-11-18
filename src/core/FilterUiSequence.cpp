// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUiSequence.h"

#include <utility>

#include "AbstractFilterUi.h"
#include "FilterResult.h"
#include "StageSequence.h"
#include "filters/fix_orientation/FilterUi.h"

FilterUiSequence::FilterUiSequence(std::shared_ptr<StageSequence> stages)
    : m_stages(std::move(stages)), m_filterUis(m_stages->count()) {
  m_filterUis[m_stages->fixOrientationFilterIdx()]
      = std::make_unique<fix_orientation::FilterUi>(m_stages->fixOrientationFilter());
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
