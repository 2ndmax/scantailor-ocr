// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <GrayImage.h>
#include <ImageId.h>
#include <filters/page_split/Settings.h>
#include <filters/page_split/SpineLineFinder.h>
#include <filters/page_split/SplitDetectionMethod.h>

#include <QDomDocument>
#include <QLineF>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace Tests {
using namespace page_split;

namespace {
// A white page with a dark vertical line at x, the way the spine looks at 100 dpi.
imageproc::GrayImage pageWithLine(const int width, const int height, const int x) {
  imageproc::GrayImage image(QSize(width, height));
  image.fill(230);
  for (int y = 0; y < height; ++y) {
    uint8_t* row = image.data() + y * image.stride();
    row[x] = 80;
    row[x + 1] = 80;
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SplitDetectionTestSuite)

BOOST_AUTO_TEST_CASE(detection_method_strings) {
  BOOST_CHECK(splitDetectionMethodFromString(splitDetectionMethodToString(EDGES_DETECTION)) == EDGES_DETECTION);
  BOOST_CHECK(splitDetectionMethodFromString(splitDetectionMethodToString(SPINE_LINE_DETECTION))
              == SPINE_LINE_DETECTION);
  BOOST_CHECK(splitDetectionMethodFromString("unknown") == EDGES_DETECTION);
}

BOOST_AUTO_TEST_CASE(default_detection_method_roundtrip_xml) {
  DefaultParams::PageSplitParams original;
  BOOST_CHECK(original.getDetectionMethod() == SPINE_LINE_DETECTION);
  original.setDetectionMethod(EDGES_DETECTION);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "page-split-params");
  doc.appendChild(el);
  BOOST_CHECK(el.attribute("detectionMethod") == "edges");

  const DefaultParams::PageSplitParams restored(doc.documentElement());
  BOOST_CHECK(restored.getDetectionMethod() == EDGES_DETECTION);
}

BOOST_AUTO_TEST_CASE(default_detection_method_missing_in_old_profiles) {
  QDomDocument doc;
  QDomElement el = doc.createElement("page-split-params");
  el.setAttribute("layoutType", "two-pages");
  doc.appendChild(el);

  const DefaultParams::PageSplitParams restored(doc.documentElement());
  BOOST_CHECK(restored.getDetectionMethod() == SPINE_LINE_DETECTION);
}

BOOST_AUTO_TEST_CASE(page_without_method_keeps_edges) {
  // Pages of projects from before the detection methods have none.
  Settings settings;
  const ImageId imageId("/scan.png");
  BOOST_CHECK(!settings.isDetectionMethodSet(imageId));
  BOOST_CHECK(settings.detectionMethod(imageId) == EDGES_DETECTION);

  settings.setDetectionMethod(imageId, SPINE_LINE_DETECTION);
  BOOST_CHECK(settings.isDetectionMethodSet(imageId));
  BOOST_CHECK(settings.detectionMethod(imageId) == SPINE_LINE_DETECTION);

  settings.clear();
  BOOST_CHECK(!settings.isDetectionMethodSet(imageId));
}

BOOST_AUTO_TEST_CASE(spine_line_is_found_upright) {
  const imageproc::GrayImage image = pageWithLine(400, 300, 190);
  double score = 0.0;
  const QLineF spine
      = SpineLineFinder::findSpine(image, QTransform(), QRectF(0, 0, 400, 300), nullptr, nullptr, &score);
  BOOST_REQUIRE(!spine.isNull());
  BOOST_CHECK(score >= SpineLineFinder::MIN_SCORE);
  // A column counts the darkest of 3 pixels, so the columns next to the line score the same,
  // and of these the one closest to the center wins.
  BOOST_CHECK(std::fabs(spine.center().x() - 190.5) <= 2.0);
  // Several tilts score the same on a clean line; the upright one wins.
  BOOST_CHECK(std::fabs(spine.p1().x() - spine.p2().x()) < 1e-6);
}

BOOST_AUTO_TEST_CASE(no_spine_on_a_blank_page) {
  imageproc::GrayImage image(QSize(400, 300));
  image.fill(230);
  double score = 1.0;
  const QLineF spine
      = SpineLineFinder::findSpine(image, QTransform(), QRectF(0, 0, 400, 300), nullptr, nullptr, &score);
  BOOST_CHECK(spine.isNull());
  BOOST_CHECK(score < SpineLineFinder::MIN_SCORE);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
