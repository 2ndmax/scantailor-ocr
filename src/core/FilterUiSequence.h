// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_FILTERUISEQUENCE_H_
#define SCANTAILOR_CORE_FILTERUISEQUENCE_H_

#include <memory>
#include <vector>

#include "NonCopyable.h"

class AbstractFilterUi;
class FilterResult;
class FilterUiInterface;
class PageInfo;
class PageSelectionAccessor;
class StageSequence;

/**
 * The user interfaces of the steps of a StageSequence, in the same order.
 */
class FilterUiSequence {
  DECLARE_NON_COPYABLE(FilterUiSequence)

 public:
  /**
   * \p pageSelectionAccessor lets the options panels apply settings to the pages selected in the
   * main window.
   */
  FilterUiSequence(std::shared_ptr<StageSequence> stages, const PageSelectionAccessor& pageSelectionAccessor);

  ~FilterUiSequence();

  /**
   * Shows the options panel of the step \p filterIdx for \p pageInfo, before the page is processed.
   */
  void preUpdateUI(int filterIdx, FilterUiInterface* ui, const PageInfo& pageInfo) const;

  /**
   * Shows \p result through the user interface of the step it belongs to. Call it after
   * result.updateUI(), which does what needs no widgets.
   */
  void showResult(FilterResult& result, FilterUiInterface* ui) const;

 private:
  std::shared_ptr<StageSequence> m_stages;
  // By the index of the step in m_stages.
  std::vector<std::unique_ptr<AbstractFilterUi>> m_filterUis;
};

#endif  // SCANTAILOR_CORE_FILTERUISEQUENCE_H_
