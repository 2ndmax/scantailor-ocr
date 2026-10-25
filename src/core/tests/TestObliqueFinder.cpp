// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPL v3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <Dpi.h>
#include <ImageTransformation.h>
#include <SkewFinder.h>
#include <filters/deskew/ObliqueFinder.h>

#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <QRectF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <optional>

namespace Tests {
using namespace deskew;
using namespace imageproc;

namespace {
const int WIDTH = 800;
const int HEIGHT = 600;
const double DEG2RAD = M_PI / 180.0;

/**
 * Lines of "text" in the block from (100, 100) to (700, 500). Each line drops by \p skewDeg from
 * left to right, and each line starts further right by \p obliqueDeg than the one above.
 */
BinaryImage textBlock(const double skewDeg, const double obliqueDeg) {
  QImage image(WIDTH, HEIGHT, QImage::Format_RGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  const double drop = 600 * std::tan(skewDeg * DEG2RAD);
  for (int y = 100; y < 500; y += 16) {
    const double shift = (y - 100) * std::tan(obliqueDeg * DEG2RAD);
    // Words of 50 pixels with gaps of 10.
    for (int x = 100; x < 700; x += 60) {
      const double left = x + shift;
      const double top = y + drop * (x - 100) / 600;
      const double topRight = y + drop * (x + 50 - 100) / 600;
      QPolygonF word;
      word << QPointF(left, top) << QPointF(left + 50, topRight) << QPointF(left + 50, topRight + 8)
           << QPointF(left, top + 8);
      painter.drawPolygon(word);
    }
  }
  painter.end();
  return BinaryImage(image);
}

ImageTransformation pageTransformation() {
  const QRectF rect(0, 0, WIDTH, HEIGHT);
  ImageTransformation xform(rect, Dpi(300, 300));
  xform.setPreCropArea(QPolygonF(rect));
  return xform;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ObliqueFinderTestSuite)

BOOST_AUTO_TEST_CASE(build_sausage_mask_null_returns_null) {
  const BinaryImage empty;
  const BinaryImage result = buildSausageMask(empty);
  BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(build_sausage_mask_preserves_size) {
  BinaryImage img(50, 30, WHITE);
  img.fill(QRect(10, 5, 20, 10), BLACK);
  const BinaryImage result = buildSausageMask(img);
  BOOST_REQUIRE(!result.isNull());
  BOOST_CHECK_EQUAL(result.width(), 50);
  BOOST_CHECK_EQUAL(result.height(), 30);
}

BOOST_AUTO_TEST_CASE(find_oblique_null_mask_returns_nullopt) {
  const BinaryImage empty;
  SkewFinder finder;
  const auto result = findObliqueDegrees(empty, finder, 5.0);
  BOOST_CHECK(!result.has_value());
}

BOOST_AUTO_TEST_CASE(deskewed_mask_turns_measured_lines_horizontal) {
  const double skewDeg = 2.0;
  const BinaryImage image(textBlock(skewDeg, 0.0));
  SkewFinder skewFinder;
  const Skew skew(skewFinder.findSkew(image));
  BOOST_TEST_MESSAGE("measured skew " << skew.angle() << ", confidence " << skew.confidence());
  BOOST_REQUIRE(skew.confidence() >= Skew::GOOD_CONFIDENCE);
  BOOST_CHECK_CLOSE(std::abs(skew.angle()), skewDeg, 10.0);

  // As in Task: the deskew angle is the negated skew.
  const double deskewDeg = -skew.angle();
  const Skew residual(skewFinder.findSkew(deskewedMask(image, deskewDeg)));
  BOOST_TEST_MESSAGE("skew after deskewedMask " << residual.angle());
  BOOST_CHECK_SMALL(residual.angle(), 0.3);

  // The page itself is rotated the same way: the start and end of a line end up at the same height.
  ImageTransformation xform(pageTransformation());
  xform.setPostRotation(deskewDeg);
  const QPointF start(xform.transform().map(QPointF(100, 300)));
  const QPointF end(xform.transform().map(QPointF(700, 300 + 600 * std::tan(skewDeg * DEG2RAD))));
  BOOST_TEST_MESSAGE("page line from " << start.y() << " to " << end.y());
  BOOST_CHECK_SMALL(end.y() - start.y(), 3.0);
}

BOOST_AUTO_TEST_CASE(oblique_angle_is_removed_from_the_page) {
  const double obliqueDeg = 3.0;
  const BinaryImage image(textBlock(0.0, obliqueDeg));
  SkewFinder skewFinder;
  const std::optional<double> found(findObliqueDegrees(image, skewFinder, 5.0));
  BOOST_REQUIRE(found.has_value());
  BOOST_TEST_MESSAGE("measured oblique " << *found);
  BOOST_CHECK_CLOSE(std::abs(*found), obliqueDeg, 15.0);

  // As in Task: the oblique angle of the page is the negated measurement. It must straighten the
  // left edge of the block completely, not only partly, too far or the other way.
  const double shiftBefore = 400 * std::tan(obliqueDeg * DEG2RAD);
  ImageTransformation xform(pageTransformation());
  xform.setPostOblique(-*found);
  const QPointF top(xform.transform().map(QPointF(100, 100)));
  const QPointF bottom(xform.transform().map(QPointF(100 + shiftBefore, 500)));
  BOOST_TEST_MESSAGE("left edge from x " << top.x() << " to x " << bottom.x());
  BOOST_CHECK_SMALL(bottom.x() - top.x(), 3.0);
}

// A fax scan with 200 x 100 dpi. The measured angles are physical ones; the mask turned by
// 90 degrees has its axes swapped, so it needs the inverse resolution ratio.
BOOST_AUTO_TEST_CASE(oblique_angle_with_unequal_dpi) {
  const double obliqueDeg = 3.0;
  const QImage physical(textBlock(0.0, obliqueDeg).toQImage());
  // Twice the pixels horizontally: stretching keeps all detail, unlike squashing.
  const BinaryImage fax(physical.scaled(2 * WIDTH, HEIGHT, Qt::IgnoreAspectRatio, Qt::FastTransformation));
  SkewFinder skewFinder;
  skewFinder.setResolutionRatio(200.0 / 100.0);
  const std::optional<double> found(findObliqueDegrees(fax, skewFinder, 5.0));
  BOOST_REQUIRE(found.has_value());
  BOOST_TEST_MESSAGE("measured oblique " << *found);
  BOOST_CHECK_CLOSE(std::abs(*found), obliqueDeg, 15.0);
  // The caller's finder is left as it was.
  BOOST_CHECK_EQUAL(skewFinder.resolutionRatio(), 2.0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
