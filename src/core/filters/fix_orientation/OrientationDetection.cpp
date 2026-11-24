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
#include "Settings.h"

#include <map>

namespace fix_orientation {

namespace {

// Minimum number of decided images before a book rotation is accepted at all.
constexpr int kMinDecidedForBookRotation = 5;
// A rotation is the book rotation only if at least this share of the decided
// images agree (60 %).
constexpr int kBookRotationMajorityPercent = 60;
// An undecided image keeps its book neighbours' rotation only when its size is
// within this many percent of the book's format on both sides.
constexpr int kMaxSizeDifferencePercent = 5;

bool similarSize(const QSize& a, const QSize& b) {
  return std::abs(a.width() - b.width()) * 100 <= b.width() * kMaxSizeDifferencePercent
         && std::abs(a.height() - b.height()) * 100 <= b.height() * kMaxSizeDifferencePercent;
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
  if (votes[best] >= kMinDecidedForBookRotation && votes[best] * 100 >= total * kBookRotationMajorityPercent) {
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

  // Selection is matched by image, so a page selected in the page view (left or
  // right half of a double page) still selects its whole image.
  std::set<ImageId> selectedImages;
  for (const PageId& id : selection) {
    selectedImages.insert(id.imageId());
  }

  // Pass 1: detect.
  std::vector<bool> processed(total, false);
  std::vector<bool> failed(total, false);
  std::vector<OrientationDetector::Result> dets(total);
  std::vector<QSize> sizes(total);
  bool cancelled = false;

  for (int i = 0; i < total; ++i) {
    status.throwIfCancelled();
    if (progress && !progress(i)) {
      cancelled = true;
      break;
    }
    const PageInfo& page = pages[i];

    bool shouldDetect = bookRule || (selectedImages.count(page.imageId()) > 0);
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
  std::vector<PageResult::Status> origin(total, PageResult::UNDECIDED);
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
        origin[i] = PageResult::FAILED;
        angle[i] = 0;
        continue;
      }
      switch (decisions[i].source) {
        case BookDecision::DETECTED:
          origin[i] = PageResult::DETECTED;
          angle[i] = decisions[i].angle;
          break;
        case BookDecision::BOOK_RULE:
          origin[i] = PageResult::BOOK_RULE;
          angle[i] = decisions[i].angle;
          break;
        default:
          origin[i] = PageResult::UNDECIDED;
          angle[i] = 0;
          break;
      }
    }
  } else {
    for (int i = 0; i < total; ++i) {
      if (!processed[i])
        continue;
      if (failed[i]) {
        origin[i] = PageResult::FAILED;
        angle[i] = 0;
      } else if (dets[i].decided) {
        origin[i] = PageResult::DETECTED;
        angle[i] = dets[i].angle;
      } else {
        origin[i] = PageResult::UNDECIDED;
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
    pr.status = origin[i];
    pr.angle = (origin[i] == PageResult::DETECTED || origin[i] == PageResult::BOOK_RULE) ? angle[i] : 0;

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

    if (pageResult) {
      pageResult(page, pr);
    }

    // set rotation if allowed
    bool selected = selectedImages.count(page.imageId()) > 0;
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

QString OrientationDetection::summary(const Result& result,
                                     const std::set<PageId>& selection,
                                     bool bookRule,
                                     const std::function<QString(const PageId&)>& pageName) {
  if (result.cancelled) {
    return tr("The detection was canceled. No pages were rotated.");
  }

  int detected = 0;
  int byBookRule = 0;
  QStringList uncertain;
  QStringList unreadable;
  for (const auto& [pageId, pageResult] : result.results) {
    if (selection.find(pageId) == selection.end()) {
      continue;
    }
    switch (pageResult.status) {
      case PageResult::DETECTED:
        ++detected;
        break;
      case PageResult::BOOK_RULE:
        ++byBookRule;
        break;
      case PageResult::UNDECIDED:
        uncertain.push_back(pageName(pageId));
        break;
      case PageResult::FAILED:
        unreadable.push_back(pageName(pageId));
        break;
    }
  }

  const int rotated = result.changed;
  const int alreadyCorrect = detected + byBookRule - rotated;

  QString text = tr("Orientation detected for %n page(s): %1 rotated, %2 already correct.", nullptr,
                    detected + byBookRule)
                     .arg(rotated)
                     .arg(alreadyCorrect);
  if (bookRule && byBookRule > 0) {
    text += "\n" + tr("Of these, %n like their neighbors or the majority.", nullptr, byBookRule);
  }
  const auto shorten = [&](const QStringList& names) {
    const int maxShown = 10;
    if (names.size() <= maxShown) {
      return names.join(QStringLiteral(", "));
    }
    QStringList shown = names.mid(0, maxShown);
    shown.push_back(tr(", and %n more", nullptr, static_cast<int>(names.size()) - maxShown));
    return shown.join(QStringLiteral(", "));
  };
  if (!uncertain.isEmpty()) {
    text += "\n\n"
            + tr("%n page(s) left unchanged because the orientation is uncertain: %1", nullptr,
                 static_cast<int>(uncertain.size()))
                  .arg(shorten(uncertain));
  }
  if (!unreadable.isEmpty()) {
    text += "\n\n" + tr("%n page(s) could not be read: %1", nullptr, static_cast<int>(unreadable.size()))
                      .arg(shorten(unreadable));
  }
  return text;
}

}  // namespace fix_orientation