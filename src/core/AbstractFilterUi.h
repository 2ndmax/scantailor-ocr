// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ABSTRACTFILTERUI_H_
#define SCANTAILOR_CORE_ABSTRACTFILTERUI_H_

class FilterResult;
class FilterUiInterface;
class PageInfo;

/**
 * The user interface of a step (AbstractFilter): what the main window shows of it. The step itself
 * only processes pages; this is the part that needs widgets.
 */
class AbstractFilterUi {
 public:
  virtual ~AbstractFilterUi() = default;

  /**
   * Shows the options panel of this step for \p pageInfo, before the page is processed.
   */
  virtual void preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) = 0;

  /**
   * Shows the result of processing a page in this step: its image view, connected to the options
   * panel. Results of another kind, such as those of batch processing, are left alone; their
   * FilterResult::updateUI() is all they need.
   */
  virtual void showResult(FilterResult& result, FilterUiInterface* ui) = 0;
};

#endif  // SCANTAILOR_CORE_ABSTRACTFILTERUI_H_
