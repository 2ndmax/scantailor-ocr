// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Dpi.h>
#include <FilterData.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <filters/select_content/ContentBoxFinder.h>
#include <filters/select_content/Dependencies.h>
#include <filters/select_content/DetectionOptions.h>
#include <filters/select_content/Params.h>

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace Tests {
using namespace select_content;

BOOST_AUTO_TEST_SUITE(SelectContentApplyTestSuite)

BOOST_AUTO_TEST_CASE(applied_pages_are_detected_again) {
  QPolygonF pageOutline;
  pageOutline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 150) << QPointF(0, 150);

  Dependencies currentDeps(pageOutline, MODE_AUTO, MODE_AUTO, false);

  // "Apply to ..." stores the target pages with empty dependencies.
  const Dependencies storedDepsWhenApplying;

  bool needUpdateContentBox = false;
  bool needUpdatePageBox = false;
  currentDeps.compatibleWith(storedDepsWhenApplying, &needUpdateContentBox, &needUpdatePageBox);

  BOOST_REQUIRE_MESSAGE(needUpdateContentBox,
                        "The empty page outline makes Task::process detect both boxes of each target page again.");
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

// A 300 x 600 dpi scan, pre-scaled to 300 dpi in both axes. The page box must mask the same
// area as in an equal-DPI scan; scaled by the original DPI per axis, it covered only the upper
// half, and content in the lower half was lost.
BOOST_AUTO_TEST_CASE(content_box_with_unequal_dpi) {
  // 4 x 6 inches: 1200 x 3600 pixels; text in the lower half, from 1 to 3 inches across
  // and from 4 to 5 inches down.
  QImage image(1200, 3600, QImage::Format_RGB32);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int y = 2400; y < 3000; y += 60) {
      for (int x = 300; x + 50 <= 900; x += 70) {
        painter.drawRect(QRect(x, y, 50, 30));
      }
    }
  }
  image.setDotsPerMeterX(static_cast<int>(std::lround(300 / 0.0254)));
  image.setDotsPerMeterY(static_cast<int>(std::lround(600 / 0.0254)));

  ImageTransformation xform(QRectF(image.rect()), Dpi(300, 600));
  xform.preScaleToEqualizeDpi();
  const FilterData data(FilterData(image), xform);
  const QRectF pageRect(xform.resultingRect());
  BOOST_REQUIRE(std::fabs(pageRect.height() - 1800) < 1);

  const QRectF content = ContentBoxFinder::findContentBox(NullTaskStatus(), data, pageRect, DetectionOptions());
  BOOST_TEST_MESSAGE("content " << content.left() << "," << content.top() << " " << content.width() << "x"
                                << content.height());
  // In the pre-scaled coordinates: x 300 to 840 (the last word ends there), y 1200 to 1500.
  BOOST_REQUIRE(!content.isEmpty());
  BOOST_CHECK(std::fabs(content.top() - 1200) < 20);
  BOOST_CHECK(std::fabs(content.bottom() - 1500) < 20);
  BOOST_CHECK(std::fabs(content.left() - 300) < 20);
  BOOST_CHECK(std::fabs(content.right() - 840) < 20);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
