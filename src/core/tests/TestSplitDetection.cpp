// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryThreshold.h>
#include <DefaultParams.h>
#include <Dpi.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <ImageId.h>
#include <ImageTransformation.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/PageLayoutEstimator.h>
#include <filters/page_split/Settings.h>
#include <filters/page_split/SpineLineFinder.h>
#include <filters/page_split/SplitDetectionMethod.h>

#include <QDomDocument>
#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
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

/**
 * A spread at 300 dpi without a folding line: two blocks of "words" with ragged edges, the left
 * one from x = 300 to 1300, the right one from 1700 to 2700, so the whitespace is around 1500.
 * \p xScale stretches it horizontally, for unequal DPI.
 */
QImage spreadWithWhitespace(const double xScale = 1.0) {
  QImage image(static_cast<int>(3000 * xScale), 2000, QImage::Format_RGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  int row = 0;
  for (int y = 300; y < 1700; y += 50, ++row) {
    const int ragged = (row * 37) % 60;
    for (const int blockLeft : {300, 1700}) {
      for (int x = blockLeft + ragged; x + 100 <= blockLeft + 1000; x += 130) {
        painter.drawRect(QRectF(x * xScale, y, 100 * xScale, 25));
      }
    }
  }
  painter.end();
  const int dpm = static_cast<int>(std::lround(300 / 0.0254));
  QImage gray = imageproc::toGrayscale(image);
  gray.setDotsPerMeterX(static_cast<int>(std::lround(dpm * xScale)));
  gray.setDotsPerMeterY(dpm);
  return gray;
}

double splitX(const PageLayout& layout) {
  return layout.inscribedCutterLine(0).center().x();
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

// The whitespace split works in the coordinates of the trimmed page, like everything after
// Fix Orientation, so a trim of 400 pixels on the left moves the line by 400 pixels.
BOOST_AUTO_TEST_CASE(whitespace_split_follows_the_trim) {
  const QImage image = spreadWithWhitespace();
  ImageTransformation xform(QRectF(image.rect()), Dpi(300, 300));
  xform.setPreCropArea(QPolygonF(QRectF(400, 0, 2600, 2000)));
  const PageLayout layout = PageLayoutEstimator::estimatePageLayout(TWO_PAGES, EDGES_DETECTION, image, xform,
                                                                    imageproc::BinaryThreshold(128));
  BOOST_TEST_MESSAGE("split at " << splitX(layout));
  BOOST_CHECK(std::fabs(splitX(layout) - 1100) < 150);
  BOOST_CHECK(layout.uncutOutline().boundingRect() == xform.resultingRect());
}

// A 600 x 300 dpi scan is pre-scaled to equal DPI; the split line has to be in those coordinates.
BOOST_AUTO_TEST_CASE(whitespace_split_with_unequal_dpi) {
  const QImage image = spreadWithWhitespace(2.0);
  ImageTransformation xform(QRectF(image.rect()), Dpi(600, 300));
  xform.preScaleToEqualizeDpi();
  const PageLayout layout = PageLayoutEstimator::estimatePageLayout(TWO_PAGES, EDGES_DETECTION, image, xform,
                                                                    imageproc::BinaryThreshold(128));
  BOOST_TEST_MESSAGE("split at " << splitX(layout) << " in " << xform.resultingRect().width());
  BOOST_CHECK(std::fabs(splitX(layout) - 1500) < 150);
}

// A folding line in a trimmed spread: the layout lies within the trimmed page.
BOOST_AUTO_TEST_CASE(folding_line_split_follows_the_trim) {
  imageproc::GrayImage gray(QSize(3000, 2000));
  gray.fill(230);
  for (int y = 0; y < gray.height(); ++y) {
    std::fill_n(gray.data() + y * gray.stride() + 1700, 8, uint8_t(40));
  }
  QImage image = gray.toQImage();
  const int dpm = static_cast<int>(std::lround(300 / 0.0254));
  image.setDotsPerMeterX(dpm);
  image.setDotsPerMeterY(dpm);
  ImageTransformation xform(QRectF(image.rect()), Dpi(300, 300));
  xform.setPreCropArea(QPolygonF(QRectF(400, 0, 2600, 2000)));
  const PageLayout layout = PageLayoutEstimator::estimatePageLayout(TWO_PAGES, EDGES_DETECTION, image, xform,
                                                                    imageproc::BinaryThreshold(128));
  BOOST_TEST_MESSAGE("split at " << splitX(layout));
  BOOST_CHECK(std::fabs(splitX(layout) - 1304) < 10);
  BOOST_CHECK(layout.uncutOutline().boundingRect() == xform.resultingRect());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
