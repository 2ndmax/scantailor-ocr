// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_LASTIMAGEVIEWTAB_H_
#define SCANTAILOR_OUTPUT_LASTIMAGEVIEWTAB_H_

#include <atomic>

#include "ImageViewTab.h"

namespace output {
/**
 * The tab of this step the user looked at last. The options panel sets it; tasks read it to know
 * what to prepare, also outside the GUI thread and without a panel.
 */
class LastImageViewTab {
 public:
  ImageViewTab get() const { return m_tab.load(); }

  void set(const ImageViewTab tab) { m_tab.store(tab); }

 private:
  // Without a panel, the tab is the one a new panel starts with.
  std::atomic<ImageViewTab> m_tab{TAB_OUTPUT};
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_LASTIMAGEVIEWTAB_H_
