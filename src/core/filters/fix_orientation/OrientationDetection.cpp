// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrientationDetection.h"

#include <QSize>

#include <cmath>
#include <set>

#include "Dpi.h"
#include "Dpm.h"
#include "ImageLoader.h"
#include "OrientationDetector.h"
#include "OrthogonalRotation.h"

#include <map>

namespace fix_orientation {

namespace {

bool similarSize(const QSize& a, const QSize& b) {
  return std::abs(a.width() - b.width()) * 20 <= b.width() && std::abs(a.height() - b.height()) * 20 <= b.height();
}

int majorityAngle(const std::vector<OrientationDetection::Detection>& scans, const std::function<bool(size_t)>& pick) {
  int votes[4] = {0, 0, 0, 0};
  int total = 0;
  for (size_t i = 0; i < scans.size(); ++i) {
    if (pick(i) && scans[i].decided) {
      ++votes[scans[i].angle / 90];
      ++total;
    }
  }
  int best = 0;
  for (int k = 1; k < 4; ++k) {
    if (votes[k] > votes[best]) {
      best = k;
    }
  }
  if (total == 0) {
    return -1;
  }
  if (votes[best] >= 5 && votes[best] * 10 >= total * 6) {
    return best * 90;
  }
  return -1;
}

QSize bookSize(const std::vector<OrientationDetection::Detection>& scans) {
  std::map<std::pair<int, int>, int> sizes;
  for (const auto& s : scans) {
    if (s.decided) {
      ++sizes[{s.width, s.height}];
    }
  }
  QSize best;
  int count = 0;
  for (const auto& kv : sizes) {
    if (kv.second > count) {
      count = kv.second;
      best = QSize(kv.first.first, kv.first.second);
    }
  }
  return best;
}

bool across(int a, int b) {
  return (a - b + 360) % 180 != 0;
}

}  // namespace

std::vector<OrientationDetection::BookDecision> OrientationDetection::applyBookRule(
    const std::vector<Detection>& scans) {
  const int n = static_cast<int>(scans.size());
  std::vector<BookDecision> out(n);
  for (int i = 0; i < n; ++i) {
    out[i].decided = scans[i].decided;
    out[i].angle = scans[i].angle;
    out[i].source = scans[i].decided ? BookDecision::DETECTED : BookDecision::UNDECIDED;
  }

  const QSize size = bookSize(scans);
  const int bookAngle = majorityAngle(scans, [](size_t) { return true; });

  for (int i = 0; i < n; ++i) {
    if (scans[i].decided) {
      if (bookAngle >= 0 && across(scans[i].angle, bookAngle)) {
        out[i].decided = true;
        out[i].angle = bookAngle;
        out[i].source = BookDecision::BOOK_RULE;
      }
      continue;
    }

    if (!similarSize(QSize(scans[i].width, scans[i].height), size)) {
      continue;
    }

    int before = -1;
    int after = -1;
    for (int j = i - 2; j >= 0; j -= 2) {
      if (scans[j].decided) {
        before = scans[j].angle;
        break;
      }
    }
    for (int j = i + 2; j < n; j += 2) {
      if (scans[j].decided) {
        after = scans[j].angle;
        break;
      }
    }

    if (before >= 0 && before == after) {
      out[i].decided = true;
      out[i].angle = before;
      out[i].source = BookDecision::BOOK_RULE;
    } else if (before >= 0 && after >= 0) {
      // Neighbours disagree: leave undecided.
    } else if (bookAngle >= 0) {
      out[i].decided = true;
      out[i].angle = bookAngle;
      out[i].source = BookDecision::BOOK_RULE;
    }
  }

  return out;
}

OrientationDetection::Result OrientationDetection::run(const std::shared_ptr<Settings>& settings,
                                                       const std::vector<PageInfo>& pages,
                                                       const std::set<PageId>& selection,
                                                       bool bookRule,
                                                       const TaskStatus& status,
                                                       const std::function<bool(int)>& progress,
                                                       const PageResultCallback& pageResult) {
  Result result;
  const int total = static_cast<int>(pages.size());

  // Pass 1: detect.
  std::vector<bool> processed(total, false);
  std::vector<bool> failed(total, false);
  std::vector<OrientationDetector::Result> dets(total);
  std::vector<QSize> sizes(total);
  bool cancelled = false;

  for (int i = 0; i < total; ++i) {
    if (progress && !progress(i)) {
      cancelled = true;
      break;
    }
    const PageInfo& page = pages[i];

    bool shouldDetect = bookRule || (selection.count(page.id()) > 0);
    if (!shouldDetect) {
      continue;
    }
    processed[i] = true;

    QImage image = ImageLoader::load(page.imageId());
    if (image.isNull()) {
      failed[i] = true;
      continue;
    }

    int dpi = page.metadata().dpi().horizontal();
    if (dpi <= 0) {
      dpi = page.metadata().dpi().vertical();
    }
    if (dpi <= 0) {
      const int stored = Dpi(Dpm(image)).horizontal();
      if (stored > 0) {
        dpi = stored;
      } else {
        dpi = 300;
      }
    }

    dets[i] = OrientationDetector::detect(image, dpi);
    sizes[i] = image.size();
  }

  if (cancelled) {
    result.cancelled = true;
    return result;
  }

  // Pass 2: book rule.
  std::vector<int> origin(total, -1);  // -1 not processed; 0 DETECTED,1 BOOK_RULE,2 UNDECIDED,3 FAILED
  std::vector<int> angle(total, 0);

  if (bookRule) {
    std::vector<Detection> detections(total);
    for (int i = 0; i < total; ++i) {
      if (!processed[i]) {
        // In book mode all are processed, but guard anyway.
        continue;
      }
      detections[i].decided = !failed[i] && dets[i].decided;
      detections[i].angle = failed[i] ? 0 : dets[i].angle;
    }
    // Need sizes for all pages, including failed.
    for (int i = 0; i < total; ++i) {
      if (!processed[i])
        continue;
      if (failed[i]) {
        detections[i].width = pages[i].metadata().size().width();
        detections[i].height = pages[i].metadata().size().height();
      } else {
        detections[i].width = sizes[i].width();
        detections[i].height = sizes[i].height();
      }
    }

    const std::vector<BookDecision> decisions = applyBookRule(detections);
    for (int i = 0; i < total; ++i) {
      if (!processed[i])
        continue;
      if (failed[i]) {
        origin[i] = 3;  // FAILED
        angle[i] = 0;
        continue;
      }
      switch (decisions[i].source) {
        case BookDecision::DETECTED:
          origin[i] = 0;
          angle[i] = decisions[i].angle;
          break;
        case BookDecision::BOOK_RULE:
          origin[i] = 1;
          angle[i] = decisions[i].angle;
          break;
        default:
          origin[i] = 2;
          angle[i] = 0;
          break;
      }
    }
  } else {
    for (int i = 0; i < total; ++i) {
      if (!processed[i])
        continue;
      if (failed[i]) {
        origin[i] = 3;
        angle[i] = 0;
      } else if (dets[i].decided) {
        origin[i] = 0;
        angle[i] = dets[i].angle;
      } else {
        origin[i] = 2;
        angle[i] = 0;
      }
    }
  }

  // Pass 3: set rotations and callbacks.
  // changed count among set pages
  for (int i = 0; i < total; ++i) {
    if (!processed[i]) {
      continue;
    }
    const PageInfo& page = pages[i];

    PageResult pr;
    if (origin[i] == 3) {
      pr.status = PageResult::FAILED;
      pr.angle = 0;
    } else if (origin[i] == 0) {
      pr.status = PageResult::DETECTED;
      pr.angle = angle[i];
    } else if (origin[i] == 1) {
      pr.status = PageResult::BOOK_RULE;
      pr.angle = angle[i];
    } else {
      pr.status = PageResult::UNDECIDED;
      pr.angle = 0;
    }

    // counters
    switch (pr.status) {
      case PageResult::DETECTED:
        ++result.detected;
        break;
      case PageResult::BOOK_RULE:
        ++result.bookRule;
        break;
      case PageResult::UNDECIDED:
        ++result.undecided;
        break;
      case PageResult::FAILED:
        ++result.failed;
        break;
    }

    result.results.emplace_back(page.id(), pr);
    if (pr.status == PageResult::DETECTED) {
      result.detectedAngles.emplace_back(page.id(), pr.angle);
    }

    if (pageResult) {
      pageResult(page, pr);
    }

    // set rotation if allowed
    bool selected = selection.count(page.id()) > 0;
    if (selected && (pr.status == PageResult::DETECTED || pr.status == PageResult::BOOK_RULE) && settings) {
      const int stored = settings->getRotationFor(page.imageId()).toDegrees();
      if (stored != pr.angle) {
        ++result.changed;
      }
      settings->applyRotation(page.imageId(), OrthogonalRotation(pr.angle));
    }
  }

  result.cancelled = false;
  return result;
}

}  // namespace fix_orientation