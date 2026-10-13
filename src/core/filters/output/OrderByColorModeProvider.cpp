// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderByColorModeProvider.h"

#include <utility>

#include "Params.h"
#include "Settings.h"

namespace output {
OrderByColorModeProvider::OrderByColorModeProvider(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)) {}

bool OrderByColorModeProvider::precedes(const PageId& lhsPage,
                                        const bool,
                                        const PageId& rhsPage,
                                        const bool) const {
  // The enum follows the order of the buttons.
  const ColorMode lhsMode = m_settings->getParams(lhsPage).colorParams().colorMode();
  const ColorMode rhsMode = m_settings->getParams(rhsPage).colorParams().colorMode();
  if (lhsMode != rhsMode) {
    return lhsMode < rhsMode;
  }
  return lhsPage < rhsPage;
}
}  // namespace output
