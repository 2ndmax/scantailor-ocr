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
#include <QRect>
#include <QRectF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <iomanip>
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

enum class Setting { JUSTIFIED, RAGGED, CENTERED };

/**
 * 25 lines of "words" between x = 100 and 700 at the top, from y = 100 to 500. Lower down, the left
 * end of the lines moves by \p leftDeg and the right end by \p rightDeg: alike for a shear,
 * opposite for a keystone. Ragged lines end short of the right edge, centered ones on both sides.
 */
BinaryImage textColumn(const double leftDeg, const double rightDeg, const Setting setting) {
  QImage image(WIDTH, HEIGHT, QImage::Format_RGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);
  for (int i = 0; i < 25; ++i) {
    const int y = 100 + 16 * i;
    double left = 100 + (y - 100) * std::tan(leftDeg * DEG2RAD);
    double right = 700 + (y - 100) * std::tan(rightDeg * DEG2RAD);
    // Fixed, uneven amounts, so that no two lines end alike.
    const double shortBy = (i * 37) % 120 + 10;
    if (setting != Setting::JUSTIFIED) {
      right -= shortBy;
    }
    if (setting == Setting::CENTERED) {
      left += shortBy;
    }
    // Words of about 50 pixels with gaps of 10, spread over the whole line like justified text.
    const int words = std::max(1, static_cast<int>((right - left + 10) / 60));
    const double wordWidth = (right - left - 10.0 * (words - 1)) / words;
    for (int w = 0; w < words; ++w) {
      painter.drawRect(QRectF(left + w * (wordWidth + 10), y, wordWidth, 8));
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

// Pins the mask rotated about its centre. Even a shift by a fraction of a pixel changes the mask
// and with it the measured oblique angle, by up to about 0.2 degrees (review DS-11).
BOOST_AUTO_TEST_CASE(deskewed_mask_keeps_its_content_in_place) {
  const BinaryImage mask(deskewedMask(textBlock(2.0, 0.0), -2.0));
  const int w = mask.width();
  const int h = mask.height();
  BOOST_CHECK_EQUAL(w, 822);
  BOOST_CHECK_EQUAL(h, 628);
  BOOST_CHECK_EQUAL(mask.countBlackPixels(), 99456);
  // A shifted content would change the share of the left and the top half.
  BOOST_CHECK_EQUAL(mask.countBlackPixels(QRect(0, 0, w / 2, h)), 50064);
  BOOST_CHECK_EQUAL(mask.countBlackPixels(QRect(0, 0, w, h / 2)), 47731);
}

// Pins the page transformation with both a rotation and an oblique angle, so that a change of
// the shear centre (review C-17) is seen if it moves the page. The values were taken from the
// code before that change.
BOOST_AUTO_TEST_CASE(rotated_and_sheared_page_stays_in_place) {
  ImageTransformation xform(pageTransformation());
  xform.setPostRotation(3.0);
  xform.setPostOblique(2.0);
  const QRectF rect(0, 0, WIDTH, HEIGHT);
  const QPointF expected[] = {QPointF(52.325320820255754, 0), QPointF(849.76685913264282, 41.868764994355068),
                              QPointF(797.44153831238702, 641.04648584709935), QPointF(0, 599.1777208527443)};
  const QPointF corners[] = {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
  for (int i = 0; i < 4; ++i) {
    const QPointF mapped(xform.transform().map(corners[i]));
    BOOST_CHECK_SMALL(mapped.x() - expected[i].x(), 1e-6);
    BOOST_CHECK_SMALL(mapped.y() - expected[i].y(), 1e-6);
  }
  const QRectF result(xform.resultingRect());
  BOOST_CHECK_SMALL(result.x(), 1e-6);
  BOOST_CHECK_SMALL(result.y(), 1e-6);
  BOOST_CHECK_SMALL(result.width() - 849.76685913264282, 1e-6);
  BOOST_CHECK_SMALL(result.height() - 641.04648584709935, 1e-6);
}

BOOST_AUTO_TEST_CASE(text_edges_of_a_sheared_column) {
  const BinaryImage column(textColumn(3.0, 3.0, Setting::JUSTIFIED));
  const TextEdges edges(measureTextEdges(column));
  BOOST_TEST_MESSAGE("edges " << edges.count << ", angle " << std::setprecision(17) << edges.angleDeg);
  // The left and the right edge.
  BOOST_CHECK_EQUAL(edges.count, 2);
  BOOST_CHECK(edges.agree);
  BOOST_CHECK_SMALL(edges.angleDeg - 3.0, 0.05);
  // Pinned when the text edges came in (2026-10): any change here changes every automatic oblique angle.
  BOOST_CHECK_CLOSE(edges.angleDeg, 2.9668095443314009, 1e-9);
  const std::optional<double> found(findObliqueFromTextEdges(column));
  BOOST_REQUIRE(found.has_value());
  BOOST_CHECK_EQUAL(*found, edges.angleDeg);
}

BOOST_AUTO_TEST_CASE(text_edges_of_an_upright_column) {
  const std::optional<double> found(findObliqueFromTextEdges(textColumn(0.0, 0.0, Setting::JUSTIFIED)));
  BOOST_REQUIRE(found.has_value());
  BOOST_CHECK_SMALL(*found, 0.05);
}

// The program applies the angle with setPostOblique(): it must straighten the left edge of the
// block completely, not only partly, too far or the other way.
BOOST_AUTO_TEST_CASE(text_edge_angle_straightens_the_page) {
  const double obliqueDeg = 3.0;
  const std::optional<double> found(findObliqueFromTextEdges(textBlock(0.0, obliqueDeg)));
  BOOST_REQUIRE(found.has_value());
  const double shiftBefore = 400 * std::tan(obliqueDeg * DEG2RAD);
  ImageTransformation xform(pageTransformation());
  xform.setPostOblique(*found);
  const QPointF top(xform.transform().map(QPointF(100, 100)));
  const QPointF bottom(xform.transform().map(QPointF(100 + shiftBefore, 500)));
  BOOST_TEST_MESSAGE("left edge from x " << top.x() << " to x " << bottom.x());
  BOOST_CHECK_SMALL(bottom.x() - top.x(), 1.0);
}

// A keystone leans the left and the right edge in opposite directions: no shear.
BOOST_AUTO_TEST_CASE(text_edges_of_a_keystone_give_no_angle) {
  const BinaryImage column(textColumn(1.0, -1.0, Setting::JUSTIFIED));
  const TextEdges edges(measureTextEdges(column));
  BOOST_CHECK_EQUAL(edges.count, 2);
  BOOST_CHECK(!edges.agree);
  BOOST_CHECK(!findObliqueFromTextEdges(column).has_value());
}

// Ragged lines have their left edge only, which cannot tell a shear from a keystone.
BOOST_AUTO_TEST_CASE(single_text_edge_gives_no_angle) {
  const BinaryImage column(textColumn(2.0, 2.0, Setting::RAGGED));
  const TextEdges edges(measureTextEdges(column));
  BOOST_CHECK_EQUAL(edges.count, 1);
  BOOST_CHECK_SMALL(edges.angleDeg - 2.0, 0.05);
  BOOST_CHECK(!findObliqueFromTextEdges(column).has_value());
}

BOOST_AUTO_TEST_CASE(centered_text_has_no_edges) {
  const BinaryImage column(textColumn(2.0, 2.0, Setting::CENTERED));
  BOOST_CHECK_EQUAL(measureTextEdges(column).count, 0);
  BOOST_CHECK(!findObliqueFromTextEdges(column).has_value());
}

BOOST_AUTO_TEST_CASE(too_few_lines_give_no_edges) {
  BinaryImage column(textColumn(2.0, 2.0, Setting::JUSTIFIED));
  // Only the top four lines are left.
  column.fill(QRect(0, 100 + 16 * 4, WIDTH, HEIGHT), WHITE);
  BOOST_CHECK_EQUAL(measureTextEdges(column).count, 0);
}

// A fax scan with 200 x 100 dpi: the angle is a physical one.
BOOST_AUTO_TEST_CASE(text_edges_with_unequal_dpi) {
  const double obliqueDeg = 3.0;
  const QImage physical(textColumn(obliqueDeg, obliqueDeg, Setting::JUSTIFIED).toQImage());
  const BinaryImage fax(physical.scaled(2 * WIDTH, HEIGHT, Qt::IgnoreAspectRatio, Qt::FastTransformation));
  const std::optional<double> found(findObliqueFromTextEdges(fax, 200.0 / 100.0));
  BOOST_REQUIRE(found.has_value());
  BOOST_CHECK_SMALL(*found - obliqueDeg, 0.05);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
