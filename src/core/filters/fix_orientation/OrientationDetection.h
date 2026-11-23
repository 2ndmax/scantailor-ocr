// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTION_H_
#define SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTION_H_

#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "PageId.h"
#include "PageInfo.h"
#include "Settings.h"
#include "TaskStatus.h"

namespace fix_orientation {

class Settings;

/**
 * Detects the orientation of the images of a project and sets the rotations in
 * the Fix Orientation settings, following ColorModeDetection's shape.
 */
class OrientationDetection {
 public:
  struct Detection {
    bool decided = false;
    int angle = 0;  // 0/90/180/270
    int width = 0;
    int height = 0;
  };

  struct BookDecision {
    enum Source { DETECTED, BOOK_RULE, UNDECIDED };
    bool decided = false;
    int angle = 0;
    Source source = UNDECIDED;
  };

  /**
   * Pure function: the book rotation rule (after andriusval/scantailor-universal)
   * applied to a sequence of per-image detection results. No images, no widgets.
   */
  static std::vector<BookDecision> applyBookRule(const std::vector<Detection>& images);

  struct PageResult {
    enum Status { DETECTED, BOOK_RULE, UNDECIDED, FAILED };
    Status status = UNDECIDED;
    int angle = 0;
  };

  using PageResultCallback = std::function<void(const PageInfo& page, const PageResult& result)>;

  struct Result {
    int detected = 0;
    int changed = 0;
    int bookRule = 0;
    int undecided = 0;
    int failed = 0;
    std::vector<std::pair<PageId, PageResult>> results;
    std::vector<std::pair<PageId, int>> detectedAngles;
    bool cancelled = false;
  };

  /**
   * \param settings Where rotations are stored.
   * \param pages Images of the project in project order (IMAGE_VIEW).
   * \param selection Pages whose rotation may be set.
   * \param bookRule Whether to apply the book rotation rule.
   */
  static Result run(const std::shared_ptr<Settings>& settings,
                    const std::vector<PageInfo>& pages,
                    const std::set<PageId>& selection,
                    bool bookRule,
                    const TaskStatus& status,
                    const std::function<bool(int)>& progress = {},
                    const PageResultCallback& pageResult = {});
};

}  // namespace fix_orientation

#endif  // SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTION_H_