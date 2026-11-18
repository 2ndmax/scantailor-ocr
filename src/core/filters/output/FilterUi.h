// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_FILTERUI_H_
#define SCANTAILOR_OUTPUT_FILTERUI_H_

#include <QCoreApplication>
#include <memory>

#include "AbstractFilterUi.h"

namespace output {
class Filter;

/**
 * What the main window shows of this step.
 */
class FilterUi : public AbstractFilterUi {
  Q_DECLARE_TR_FUNCTIONS(output::FilterUi)
 public:
  explicit FilterUi(std::shared_ptr<Filter> filter);

  void showResult(FilterResult& result, FilterUiInterface* ui) override;

 private:
  std::shared_ptr<Filter> m_filter;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_FILTERUI_H_
