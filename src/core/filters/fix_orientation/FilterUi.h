// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_
#define SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_

#include <memory>

#include "AbstractFilterUi.h"

namespace fix_orientation {
class Filter;

/**
 * What the main window shows of this step.
 */
class FilterUi : public AbstractFilterUi {
 public:
  explicit FilterUi(std::shared_ptr<Filter> filter);

  void showResult(FilterResult& result, FilterUiInterface* ui) override;

 private:
  std::shared_ptr<Filter> m_filter;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_FILTERUI_H_
