// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/select_content/Dependencies.h>

#include <QDomDocument>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace select_content;

BOOST_AUTO_TEST_SUITE(SelectContentApplyTestSuite)

BOOST_AUTO_TEST_CASE(apply_content_box_auto_forces_recompute) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  Dependencies currentDeps(pageOutline, MODE_AUTO, MODE_DISABLED, false, DetectionOptions());

  Dependencies storedDepsWhenApplyingAuto(QPolygonF(), MODE_DISABLED, MODE_DISABLED, false, DetectionOptions());

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

  Dependencies deps1(pageOutline, MODE_AUTO, MODE_DISABLED, false, DetectionOptions());
  Dependencies deps2(pageOutline, MODE_AUTO, MODE_DISABLED, false, DetectionOptions());

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

  Dependencies currentDeps(outlineA, MODE_AUTO, MODE_DISABLED, false, DetectionOptions());
  Dependencies storedDeps(outlineB, MODE_AUTO, MODE_DISABLED, false, DetectionOptions());

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  currentDeps.compatibleWith(storedDeps, &needUpdateContentBox, &needUpdatePageBox);

  BOOST_REQUIRE_MESSAGE(needUpdateContentBox,
                        "When outlines differ, needUpdateContentBox must be true (PR #108 review).");
  BOOST_CHECK(needUpdatePageBox);
}

BOOST_AUTO_TEST_CASE(changed_detection_options_update_auto_content_box_only) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  for (const DetectionOptions& changed : {DetectionOptions(90, 0), DetectionOptions(65, 40)}) {
    Dependencies currentDeps(pageOutline, MODE_AUTO, MODE_AUTO, false, changed);
    Dependencies storedDeps(pageOutline, MODE_AUTO, MODE_AUTO, false, DetectionOptions());

    bool needUpdateContentBox = false;
    bool needUpdatePageBox = false;
    BOOST_CHECK(!currentDeps.compatibleWith(storedDeps, &needUpdateContentBox, &needUpdatePageBox));
    BOOST_CHECK(needUpdateContentBox);
    BOOST_CHECK(!needUpdatePageBox);
  }
}

BOOST_AUTO_TEST_CASE(changed_detection_options_keep_manual_content_box) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  Dependencies currentDeps(pageOutline, MODE_MANUAL, MODE_DISABLED, false, DetectionOptions(90, 40));
  Dependencies storedDeps(pageOutline, MODE_MANUAL, MODE_DISABLED, false, DetectionOptions());

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  BOOST_CHECK(currentDeps.compatibleWith(storedDeps, &needUpdateContentBox, &needUpdatePageBox));
  BOOST_CHECK(!needUpdateContentBox);
}

BOOST_AUTO_TEST_CASE(detection_options_xml) {
  QDomDocument doc;
  QDomElement el(doc.createElement("params"));
  DetectionOptions(90, 40).writeTo(el);
  BOOST_CHECK(DetectionOptions(el) == DetectionOptions(90, 40));

  // Projects saved before these settings existed get the former behavior.
  const DetectionOptions fromOldProject(doc.createElement("params"));
  BOOST_CHECK_EQUAL(fromOldProject.fillFactor(), DetectionOptions::DEFAULT_FILL_FACTOR);
  BOOST_CHECK_EQUAL(fromOldProject.borderTolerance(), DetectionOptions::DEFAULT_BORDER_TOLERANCE);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
