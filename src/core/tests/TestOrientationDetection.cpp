// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "filters/fix_orientation/OrientationDetection.h"

#include <QSize>
#include <boost/test/unit_test.hpp>
#include <vector>

using fix_orientation::OrientationDetection;

namespace {
OrientationDetection::Detection makeDet(bool decided, int angle, int w, int h) {
  OrientationDetection::Detection d;
  d.decided = decided;
  d.angle = angle;
  d.width = w;
  d.height = h;
  return d;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OrientationDetectionBookRuleSuite)

// Step 1: fewer than 5 decided images -> no book rotation.
BOOST_AUTO_TEST_CASE(no_book_rotation_under_five) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 4; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  imgs.push_back(makeDet(false, 0, 100, 200));
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  for (size_t i = 0; i < 4; ++i) {
    BOOST_CHECK(res[i].decided);
    BOOST_CHECK_EQUAL(res[i].angle, 0);
    BOOST_CHECK_EQUAL(res[i].source, OrientationDetection::BookDecision::DETECTED);
  }
  BOOST_CHECK(!res[4].decided);
  BOOST_CHECK_EQUAL(res[4].source, OrientationDetection::BookDecision::UNDECIDED);
}

// Step 1: under 60 % majority -> no book rotation.
BOOST_AUTO_TEST_CASE(no_book_rotation_under_sixty_percent) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 3; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  for (int i = 0; i < 3; ++i) {
    imgs.push_back(makeDet(true, 180, 100, 200));
  }
  imgs.push_back(makeDet(false, 0, 100, 200));
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  for (size_t i = 0; i < 6; ++i) {
    BOOST_CHECK(res[i].decided);
  }
  BOOST_CHECK(!res[6].decided);
  BOOST_CHECK_EQUAL(res[6].source, OrientationDetection::BookDecision::UNDECIDED);
}

// Step 3: an undecided image in a different format stays undecided.
BOOST_AUTO_TEST_CASE(different_format_stays) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 6; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  imgs.push_back(makeDet(false, 0, 200, 300));  // different format
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  for (size_t i = 0; i < 6; ++i) {
    BOOST_CHECK(res[i].decided);
    BOOST_CHECK_EQUAL(res[i].angle, 0);
  }
  BOOST_CHECK(!res[6].decided);
  BOOST_CHECK_EQUAL(res[6].source, OrientationDetection::BookDecision::UNDECIDED);
}

// Step 4: both neighbours of equal parity agree -> their rotation.
BOOST_AUTO_TEST_CASE(neighbors_equal) {
  std::vector<OrientationDetection::Detection> imgs;
  imgs.push_back(makeDet(true, 180, 100, 200));  // 0
  imgs.push_back(makeDet(true, 0, 100, 200));    // 1 decided (other parity)
  imgs.push_back(makeDet(false, 0, 100, 200));   // 2 undecided
  imgs.push_back(makeDet(true, 0, 100, 200));    // 3 decided (other parity)
  imgs.push_back(makeDet(true, 180, 100, 200));  // 4 decided, same parity as 2
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  BOOST_CHECK(res[2].decided);
  BOOST_CHECK_EQUAL(res[2].angle, 180);
  BOOST_CHECK_EQUAL(res[2].source, OrientationDetection::BookDecision::BOOK_RULE);
  // the others keep their detected values
  BOOST_CHECK_EQUAL(res[0].angle, 180);
  BOOST_CHECK_EQUAL(res[0].source, OrientationDetection::BookDecision::DETECTED);
  BOOST_CHECK_EQUAL(res[1].angle, 0);
  BOOST_CHECK_EQUAL(res[4].angle, 180);
}

// Step 4: both neighbours present but disagree -> stays undecided.
BOOST_AUTO_TEST_CASE(neighbors_different) {
  std::vector<OrientationDetection::Detection> imgs;
  imgs.push_back(makeDet(true, 0, 100, 200));  // 0
  imgs.push_back(
      makeDet(false, 0, 100, 200));  // 1 undecided (parity odd, only one parity neighbour -> need index 2 case)
  // use an undecided at even index 2 with neighbours 0 and 4 differing
  std::vector<OrientationDetection::Detection> imgs2;
  imgs2.push_back(makeDet(true, 0, 100, 200));    // 0
  imgs2.push_back(makeDet(true, 0, 100, 200));    // 1
  imgs2.push_back(makeDet(false, 0, 100, 200));   // 2 undecided
  imgs2.push_back(makeDet(true, 0, 100, 200));    // 3
  imgs2.push_back(makeDet(true, 180, 100, 200));  // 4
  // book angle 0 (majority) exists, but step 4 takes precedence when both present
  for (int i = 0; i < 5; ++i) {
    imgs2.push_back(makeDet(true, 0, 100, 200));
  }
  const auto res = OrientationDetection::applyBookRule(imgs2);
  BOOST_REQUIRE_EQUAL(res.size(), imgs2.size());
  BOOST_CHECK(!res[2].decided);
  BOOST_CHECK_EQUAL(res[2].source, OrientationDetection::BookDecision::UNDECIDED);
}

// Step 4: only one neighbour (end of book) -> book rotation.
BOOST_AUTO_TEST_CASE(start_end_book) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 6; ++i) {
    imgs.push_back(makeDet(true, 90, 100, 200));
  }
  imgs.push_back(makeDet(false, 0, 100, 200));  // last, one neighbour
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  BOOST_CHECK(res[6].decided);
  BOOST_CHECK_EQUAL(res[6].angle, 90);
  BOOST_CHECK_EQUAL(res[6].source, OrientationDetection::BookDecision::BOOK_RULE);
  for (size_t i = 0; i < 6; ++i) {
    BOOST_CHECK(res[i].decided);
    BOOST_CHECK_EQUAL(res[i].angle, 90);
    BOOST_CHECK_EQUAL(res[i].source, OrientationDetection::BookDecision::DETECTED);
  }
}

// Step 5: a decided image rotated 90 to the book rotation becomes the book rotation.
BOOST_AUTO_TEST_CASE(perpendicular_decided) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 6; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  imgs.push_back(makeDet(true, 90, 100, 200));
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  BOOST_CHECK(res[6].decided);
  BOOST_CHECK_EQUAL(res[6].angle, 0);
  BOOST_CHECK_EQUAL(res[6].source, OrientationDetection::BookDecision::BOOK_RULE);
  for (size_t i = 0; i < 6; ++i) {
    BOOST_CHECK(res[i].decided);
    BOOST_CHECK_EQUAL(res[i].angle, 0);
    BOOST_CHECK_EQUAL(res[i].source, OrientationDetection::BookDecision::DETECTED);
  }
}

// Step 5: a decided image 180 apart keeps its rotation.
BOOST_AUTO_TEST_CASE(opposite_stays) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 6; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  imgs.push_back(makeDet(true, 180, 100, 200));
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  BOOST_CHECK(res[6].decided);
  BOOST_CHECK_EQUAL(res[6].angle, 180);
  BOOST_CHECK_EQUAL(res[6].source, OrientationDetection::BookDecision::DETECTED);
}

// Book A: alternating 180/0, scan0028 (index 6) undecided -> 180 via neighbours.
BOOST_AUTO_TEST_CASE(book_a_main) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 20; ++i) {
    if (i == 6) {
      imgs.push_back(makeDet(false, 0, 100, 200));
    } else {
      imgs.push_back(makeDet(true, (i % 2 == 0) ? 180 : 0, 100, 200));
    }
  }
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  for (int i = 0; i < 20; ++i) {
    if (i == 6) {
      BOOST_CHECK(res[i].decided);
      BOOST_CHECK_EQUAL(res[i].angle, 180);
      BOOST_CHECK_EQUAL(res[i].source, OrientationDetection::BookDecision::BOOK_RULE);
    } else {
      BOOST_CHECK(res[i].decided);
      BOOST_CHECK_EQUAL(res[i].angle, (i % 2 == 0) ? 180 : 0);
      BOOST_CHECK_EQUAL(res[i].source, OrientationDetection::BookDecision::DETECTED);
    }
  }
}

// Book A without scan0024 (index 2): scan0028 now at index 5, becomes 180.
BOOST_AUTO_TEST_CASE(book_a_without_0024) {
  std::vector<int> originalIndices;
  for (int i = 0; i < 20; ++i) {
    if (i == 2) {
      continue;
    }
    originalIndices.push_back(i);
  }
  std::vector<OrientationDetection::Detection> imgs;
  for (int oi : originalIndices) {
    if (oi == 6) {
      imgs.push_back(makeDet(false, 0, 100, 200));
    } else {
      imgs.push_back(makeDet(true, (oi % 2 == 0) ? 180 : 0, 100, 200));
    }
  }
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  for (size_t k = 0; k < imgs.size(); ++k) {
    const int oi = originalIndices[k];
    if (oi == 6) {
      BOOST_CHECK(res[k].decided);
      BOOST_CHECK_EQUAL(res[k].angle, 180);
      BOOST_CHECK_EQUAL(res[k].source, OrientationDetection::BookDecision::BOOK_RULE);
    } else {
      BOOST_CHECK(res[k].decided);
      BOOST_CHECK_EQUAL(res[k].angle, (oi % 2 == 0) ? 180 : 0);
      BOOST_CHECK_EQUAL(res[k].source, OrientationDetection::BookDecision::DETECTED);
    }
  }
}

// Gap directly next to the undecided image: stays undecided.
BOOST_AUTO_TEST_CASE(gap_next_to_undecided) {
  std::vector<int> originalIndices;
  for (int i = 0; i < 20; ++i) {
    if (i == 5) {
      continue;  // scan0027 missing
    }
    originalIndices.push_back(i);
  }
  std::vector<OrientationDetection::Detection> imgs;
  for (int oi : originalIndices) {
    if (oi == 6) {
      imgs.push_back(makeDet(false, 0, 100, 200));
    } else {
      imgs.push_back(makeDet(true, (oi % 2 == 0) ? 180 : 0, 100, 200));
    }
  }
  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  // find the undecided one (original index 6)
  for (size_t k = 0; k < imgs.size(); ++k) {
    if (originalIndices[k] == 6) {
      BOOST_CHECK(!res[k].decided);
      BOOST_CHECK_EQUAL(res[k].source, OrientationDetection::BookDecision::UNDECIDED);
    } else {
      BOOST_CHECK(res[k].decided);
      BOOST_CHECK_EQUAL(res[k].angle, (originalIndices[k] % 2 == 0) ? 180 : 0);
      BOOST_CHECK_EQUAL(res[k].source, OrientationDetection::BookDecision::DETECTED);
    }
  }
}

// A long list of uncertain pages names the first ten and counts the rest, without a doubled comma.
BOOST_AUTO_TEST_CASE(summary_shortens_a_long_list) {
  OrientationDetection::Result result;
  std::set<PageId> selection;
  for (int i = 0; i < 12; ++i) {
    const PageId page(ImageId(QStringLiteral("p%1.png").arg(i)), PageId::SINGLE_PAGE);
    OrientationDetection::PageResult pageResult;
    pageResult.status = OrientationDetection::PageResult::UNDECIDED;
    result.results.emplace_back(page, pageResult);
    result.undecided += 1;
    selection.insert(page);
  }
  const QString text = OrientationDetection::summary(result, selection, false,
                                                     [](const PageId& page) { return page.imageId().filePath(); });
  BOOST_CHECK_MESSAGE(text.endsWith(QStringLiteral("p8.png, p9.png, and 2 more")), text.toStdString());
}

BOOST_AUTO_TEST_CASE(summary_filters_by_image_id_across_page_views) {
  OrientationDetection::Result result;
  const PageId imgPage(ImageId(QStringLiteral("doc.png")), PageId::SINGLE_PAGE);
  const PageId selectedLeft(ImageId(QStringLiteral("doc.png")), PageId::LEFT_PAGE);
  OrientationDetection::PageResult pageResult;
  pageResult.status = OrientationDetection::PageResult::DETECTED;
  pageResult.angle = 90;
  result.results.emplace_back(imgPage, pageResult);
  result.detected = 1;
  result.changed = 1;

  const std::set<PageId> selection{selectedLeft};
  const QString text = OrientationDetection::summary(result, selection, false,
                                                     [](const PageId& page) { return page.imageId().filePath(); });
  BOOST_CHECK_MESSAGE(text.contains(QStringLiteral("1 rotated, 0 already correct")), text.toStdString());
  BOOST_CHECK(!text.contains(QStringLiteral("-1")));
}

BOOST_AUTO_TEST_CASE(book_rule_ignores_invalid_angles_and_zero_sizes) {
  std::vector<OrientationDetection::Detection> imgs;
  for (int i = 0; i < 6; ++i) {
    imgs.push_back(makeDet(true, 0, 100, 200));
  }
  // Out-of-range / non-orthogonal decided angles must not corrupt stack votes.
  imgs.push_back(makeDet(true, 360, 100, 200));
  imgs.push_back(makeDet(true, -90, 100, 200));
  imgs.push_back(makeDet(true, 45, 100, 200));
  // 0x0 undecided scan must not match non-zero book size.
  imgs.push_back(makeDet(false, 0, 0, 0));

  const auto res = OrientationDetection::applyBookRule(imgs);
  BOOST_REQUIRE_EQUAL(res.size(), imgs.size());
  BOOST_CHECK(!res.back().decided);
  BOOST_CHECK_EQUAL(res.back().source, OrientationDetection::BookDecision::UNDECIDED);
}

BOOST_AUTO_TEST_CASE(book_rule_empty_and_boundary_inputs) {
  BOOST_CHECK(OrientationDetection::applyBookRule({}).empty());

  // Fewer than 5 votes for the best angle (4 vs 2): book rotation does not trigger.
  std::vector<OrientationDetection::Detection> belowMin{
      makeDet(true, 0, 100, 200),  makeDet(true, 0, 100, 200),   makeDet(true, 0, 100, 200),
      makeDet(true, 0, 100, 200),  makeDet(true, 180, 100, 200), makeDet(true, 180, 100, 200),
      makeDet(false, 0, 100, 200),
  };
  const auto resBelow = OrientationDetection::applyBookRule(belowMin);
  BOOST_REQUIRE_EQUAL(resBelow.size(), belowMin.size());
  BOOST_CHECK(!resBelow.back().decided);

  // 6 votes for 0° and 4 votes for 180° (votes[0] >= 5 and exact 60% majority) -> decides matching-size undecided page.
  std::vector<OrientationDetection::Detection> boundary{
      makeDet(true, 0, 100, 200),   makeDet(true, 0, 100, 200),   makeDet(true, 0, 100, 200),
      makeDet(true, 0, 100, 200),   makeDet(true, 0, 100, 200),   makeDet(true, 0, 100, 200),
      makeDet(true, 180, 100, 200), makeDet(true, 180, 100, 200), makeDet(true, 180, 100, 200),
      makeDet(true, 180, 100, 200), makeDet(false, 0, 100, 200),
  };
  const auto res = OrientationDetection::applyBookRule(boundary);
  BOOST_REQUIRE_EQUAL(res.size(), boundary.size());
  BOOST_CHECK(res.back().decided);
  BOOST_CHECK_EQUAL(res.back().angle, 0);
  BOOST_CHECK_EQUAL(res.back().source, OrientationDetection::BookDecision::BOOK_RULE);
}

BOOST_AUTO_TEST_CASE(summary_covers_cancelled_book_rule_failed_and_split_selection) {
  OrientationDetection::Result cancelledRes;
  cancelledRes.cancelled = true;
  const QString cancelledMsg
      = OrientationDetection::summary(cancelledRes, {}, false, [](const PageId& p) { return p.imageId().filePath(); });
  BOOST_CHECK(cancelledMsg.contains(QStringLiteral("canceled"), Qt::CaseInsensitive));

  OrientationDetection::Result result;
  const ImageId imgDetected(QStringLiteral("d.png"));
  const ImageId imgBook(QStringLiteral("b.png"));
  const ImageId imgFailed(QStringLiteral("f.png"));
  result.results.push_back(
      {PageId(imgDetected, PageId::SINGLE_PAGE), {OrientationDetection::PageResult::DETECTED, 90}});
  result.results.push_back({PageId(imgBook, PageId::SINGLE_PAGE), {OrientationDetection::PageResult::BOOK_RULE, 90}});
  result.results.push_back({PageId(imgFailed, PageId::SINGLE_PAGE), {OrientationDetection::PageResult::FAILED, 0}});
  result.detected = 1;
  result.bookRule = 1;
  result.changed = 1;
  result.failed = 1;

  // Selecting both halves of a split image still counts that image only once.
  const std::set<PageId> selection{
      PageId(imgDetected, PageId::LEFT_PAGE),
      PageId(imgDetected, PageId::RIGHT_PAGE),
      PageId(imgBook, PageId::SINGLE_PAGE),
      PageId(imgFailed, PageId::SINGLE_PAGE),
  };
  const QString text = OrientationDetection::summary(result, selection, true,
                                                     [](const PageId& page) { return page.imageId().filePath(); });
  BOOST_CHECK_MESSAGE(text.contains(QStringLiteral("2 page(s): 1 rotated, 1 already correct")), text.toStdString());
  BOOST_CHECK_MESSAGE(text.contains(QStringLiteral("Of these, 1 like their neighbors")), text.toStdString());
  BOOST_CHECK_MESSAGE(text.contains(QStringLiteral("could not be read: f.png")), text.toStdString());
}

BOOST_AUTO_TEST_SUITE_END()