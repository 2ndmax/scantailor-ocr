// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_ORDERBYCOLORMODEPROVIDER_H_
#define SCANTAILOR_OUTPUT_ORDERBYCOLORMODEPROVIDER_H_

#include <memory>
#include <vector>

#include "ColorParams.h"
#include "PageOrderProvider.h"

namespace output {
class Settings;

/**
 * Orders the pages by color mode, in the order of the buttons: black and white, color / grayscale,
 * mixed. Pages with the same color mode keep their natural order. The color mode is a setting, so
 * pages not processed yet are ordered the same way.
 */
class OrderByColorModeProvider : public PageOrderProvider {
 public:
  explicit OrderByColorModeProvider(std::shared_ptr<Settings> settings);

  bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const override;

  Comparator snapshot(const std::vector<PageId>& pages) const override;

 private:
  static ColorMode colorModeOf(const Settings& settings, const PageId& page);

  static bool compare(ColorMode lhsMode, const PageId& lhsPage, ColorMode rhsMode, const PageId& rhsPage);

  std::shared_ptr<Settings> m_settings;
};
}  // namespace output

#endif  // SCANTAILOR_OUTPUT_ORDERBYCOLORMODEPROVIDER_H_
