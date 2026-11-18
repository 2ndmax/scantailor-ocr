// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_
#define SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_

#include <memory>

#include "AbstractFilterUi.h"
#include "PageSelectionAccessor.h"
#include "SafeDeletingQObjectPtr.h"

class PageInfo;

namespace fix_orientation {
class Filter;
class OptionsWidget;

/**
 * What the main window shows of this step.
 */
class FilterUi : public AbstractFilterUi {
 public:
  FilterUi(std::shared_ptr<Filter> filter, const PageSelectionAccessor& pageSelectionAccessor);

  void preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) override;

  void showResult(FilterResult& result, FilterUiInterface* ui) override;

  /**
   * The options panel of this step. It is created on first use, from the GUI thread.
   */
  OptionsWidget* optionsWidget();

 private:
  std::shared_ptr<Filter> m_filter;
  PageSelectionAccessor m_pageSelectionAccessor;
  SafeDeletingQObjectPtr<OptionsWidget> m_optionsWidget;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_
