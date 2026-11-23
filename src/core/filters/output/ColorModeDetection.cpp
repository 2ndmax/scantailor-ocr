// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorModeDetection.h"

#include "CompositeCacheDrivenTask.h"
#include "Filter.h"
#include "GeometryCollector.h"
#include "TaskStatus.h"

namespace output {

ColorModeDetection::Result ColorModeDetection::run(const std::shared_ptr<Filter>& filter,
                                                   const std::vector<PageInfo>& pages,
                                                   const std::shared_ptr<CompositeCacheDrivenTask>& geometryTask,
                                                   const TaskStatus& status,
                                                   const std::function<bool(int)>& progress,
                                                   const PageResultCallback& pageResult) {
  Result result;
  const int total = static_cast<int>(pages.size());
  for (int i = 0; i < total; ++i) {
    if (progress && !progress(i)) {
      result.cancelled = true;
      break;
    }
    GeometryCollector geometry;
    geometryTask->process(pages[i], &geometry);
    if (!geometry.hasGeometry()) {
      ++result.skipped;
      if (pageResult) {
        PageResult pr;
        pr.status = PageResult::SKIPPED;
        pageResult(pages[i], pr);
      }
      continue;
    }
    ColorMode mode = COLOR_GRAYSCALE;
    if (!filter->detectColorMode(pages[i], geometry, status, &mode)) {
      ++result.failed;
      if (pageResult) {
        PageResult pr;
        pr.status = PageResult::FAILED;
        pageResult(pages[i], pr);
      }
      continue;
    }
    switch (mode) {
      case BLACK_AND_WHITE:
        ++result.blackAndWhite;
        break;
      case COLOR_GRAYSCALE:
        ++result.colorGrayscale;
        break;
      case MIXED:
        ++result.mixed;
        break;
    }
    result.detected.emplace_back(pages[i].id(), mode);
    if (pageResult) {
      PageResult pr;
      pr.status = PageResult::DETECTED;
      pr.mode = mode;
      pageResult(pages[i], pr);
    }
  }
  return result;
}

}  // namespace output
