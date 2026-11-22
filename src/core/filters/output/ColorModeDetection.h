// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_COLORMODEDETECTION_H_
#define SCANTAILOR_OUTPUT_COLORMODEDETECTION_H_

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include "ColorParams.h"
#include "PageId.h"
#include "PageInfo.h"

class CompositeCacheDrivenTask;
class TaskStatus;

namespace output {
class Filter;

/**
 * The color mode of the pages, detected through output::Filter::detectColorMode(). The geometry of
 * each page comes from the steps before the Output step, through the CompositeCacheDrivenTask, so
 * no image but the source is processed. The counts and the detected pages are the ones the app
 * reports; the command line tool reports them per page.
 */
class ColorModeDetection {
 public:
  struct Result {
    int blackAndWhite = 0;
    int colorGrayscale = 0;
    int mixed = 0;
    int skipped = 0;
    int failed = 0;
    /** The pages whose mode was detected, in the order of the pages. */
    std::vector<std::pair<PageId, ColorMode>> detected;
    /** The detection stopped before the last page. */
    bool cancelled = false;
  };

  /**
   * Detects the color mode of each page of \p pages.
   *
   * \param filter The output filter whose detectColorMode() sets the mode of each page.
   * \param pages The pages to look at.
   * \param geometryTask The cache-driven task of the steps up to the Margins step; one whose steps
   *        ran less finds no geometry and the page is skipped.
   * \param status Passed to detectColorMode().
   * \param progress Called before each page with the number of pages done so far, from 0; the run
   *        stops when it returns false.
   */
  static Result run(const std::shared_ptr<Filter>& filter,
                    const std::vector<PageInfo>& pages,
                    const std::shared_ptr<CompositeCacheDrivenTask>& geometryTask,
                    const TaskStatus& status,
                    const std::function<bool(int)>& progress = {});
};
}  // namespace output

#endif  // SCANTAILOR_OUTPUT_COLORMODEDETECTION_H_
