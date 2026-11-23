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


}  // namespace fix_orientation
