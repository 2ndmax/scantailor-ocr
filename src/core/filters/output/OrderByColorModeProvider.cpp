// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrderByColorModeProvider.h"

#include <utility>

#include "Params.h"
#include "Settings.h"

namespace output {
OrderByColorModeProvider::OrderByColorModeProvider(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)) {}

ColorMode OrderByColorModeProvider::colorModeOf(const Settings& settings, const PageId& page) {
  return settings.getParams(page).colorParams().colorMode();
}

bool OrderByColorModeProvider::compare(const ColorMode lhsMode,
                                       const PageId& lhsPage,
                                       const ColorMode rhsMode,
                                       const PageId& rhsPage) {
  // The enum follows the order of the buttons.
  if (lhsMode != rhsMode) {
    return lhsMode < rhsMode;
  }
  return lhsPage < rhsPage;
}

bool OrderByColorModeProvider::precedes(const PageId& lhsPage, const bool, const PageId& rhsPage, const bool) const {
  return compare(colorModeOf(*m_settings, lhsPage), lhsPage, colorModeOf(*m_settings, rhsPage), rhsPage);
}

PageOrderProvider::Comparator OrderByColorModeProvider::snapshot(const std::vector<PageId>& pages) const {
  return snapshotOf<ColorMode>(
      pages, [settings = m_settings](const PageId& page) { return colorModeOf(*settings, page); },
      [](const ColorMode lhsMode, bool, const PageId& lhsPage, const ColorMode rhsMode, bool, const PageId& rhsPage) {
        return compare(lhsMode, lhsPage, rhsMode, rhsPage);
      });
}
}  // namespace output
