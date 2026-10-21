// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/select_content/Dependencies.h>
#include <filters/select_content/Params.h>

#include <QDomDocument>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace select_content;

BOOST_AUTO_TEST_SUITE(SelectContentApplyTestSuite)

BOOST_AUTO_TEST_CASE(apply_content_box_auto_forces_recompute) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  Dependencies currentDeps(pageOutline, MODE_AUTO, MODE_DISABLED, false);

  Dependencies storedDepsWhenApplyingAuto(QPolygonF(), MODE_DISABLED, MODE_DISABLED, false);

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  currentDeps.compatibleWith(storedDepsWhenApplyingAuto, &needUpdateContentBox, &needUpdatePageBox);

  BOOST_REQUIRE_MESSAGE(needUpdateContentBox,
                        "When applying 'Content Box: Auto' we store deps with MODE_DISABLED so that "
                        "Task::process recomputes content box for each target page.");
  BOOST_CHECK(needUpdatePageBox);
}

BOOST_AUTO_TEST_CASE(same_mode_auto_no_content_box_update) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  Dependencies deps1(pageOutline, MODE_AUTO, MODE_DISABLED, false);
  Dependencies deps2(pageOutline, MODE_AUTO, MODE_DISABLED, false);

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  deps1.compatibleWith(deps2, &needUpdateContentBox, &needUpdatePageBox);

  BOOST_CHECK(!needUpdateContentBox);
  BOOST_CHECK(!needUpdatePageBox);
}

BOOST_AUTO_TEST_CASE(different_outlines_force_content_box_update) {
  QPolygonF outlineA;
  outlineA << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);
  QPolygonF outlineB;
  outlineB << QPointF(0, 0) << QPointF(120, 0) << QPointF(120, 150) << QPointF(0, 150);

  Dependencies currentDeps(outlineA, MODE_AUTO, MODE_DISABLED, false);
  Dependencies storedDeps(outlineB, MODE_AUTO, MODE_DISABLED, false);

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  currentDeps.compatibleWith(storedDeps, &needUpdateContentBox, &needUpdatePageBox);

  BOOST_REQUIRE_MESSAGE(needUpdateContentBox,
                        "When outlines differ, needUpdateContentBox must be true (PR #108 review).");
  BOOST_CHECK(needUpdatePageBox);
}

BOOST_AUTO_TEST_CASE(changed_detection_options_detect_the_content_box_again) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);
  const DetectionOptions oldOptions(65, 0, false);
  const DetectionOptions newOptions(80, 0, false);

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false, newOptions)
      .compatibleWith(Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false, oldOptions), &needUpdateContentBox,
                      &needUpdatePageBox);
  BOOST_CHECK(needUpdateContentBox);
  BOOST_CHECK(!needUpdatePageBox);

  // A content box set by hand doesn't depend on them.
  BOOST_CHECK(Dependencies(pageOutline, MODE_MANUAL, MODE_DISABLED, false, newOptions)
                  .compatibleWith(Dependencies(pageOutline, MODE_MANUAL, MODE_DISABLED, false, oldOptions)));
}

BOOST_AUTO_TEST_CASE(pages_of_older_projects_keep_their_content_box) {
  // Neither the page nor its stored dependencies know the settings it was detected with.
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);
  const Dependencies stored(pageOutline, MODE_AUTO, MODE_DISABLED, false);
  BOOST_CHECK(Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false).compatibleWith(stored));
  BOOST_CHECK(Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false, DetectionOptions(80, 0, false))
                  .compatibleWith(stored));
}

BOOST_AUTO_TEST_CASE(detection_options_are_stored_with_the_page) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);
  const DetectionOptions options(90, 30, true);
  Params params(Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false, options));
  params.setDetectionOptions(options);

  QDomDocument doc;
  const Params read(params.toXml(doc, "params"));
  BOOST_REQUIRE(read.detectionOptions());
  BOOST_CHECK(*read.detectionOptions() == options);
  // The dependencies, too, so that a later change detects the content box again.
  bool needUpdateContentBox = false;
  Dependencies(pageOutline, MODE_AUTO, MODE_DISABLED, false, DetectionOptions(65, 0, false))
      .compatibleWith(read.dependencies(), &needUpdateContentBox, nullptr);
  BOOST_CHECK(needUpdateContentBox);

  // Pages of older projects have none.
  QDomDocument oldDoc;
  const Params oldPage(Params(Dependencies(pageOutline)).toXml(oldDoc, "params"));
  BOOST_CHECK(!oldPage.detectionOptions());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
