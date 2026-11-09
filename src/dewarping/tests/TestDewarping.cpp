// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <Constants.h>
#include <Curve.h>
#include <CylindricalSurfaceDewarper.h>
#include <DistortionModel.h>
#include <DistortionModelBuilder.h>
#include <Dpi.h>
#include <GrayImage.h>
#include <RasterDewarper.h>
#include <SEDM.h>
#include <TaskStatus.h>
#include <TextLineTracer.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <vector>

namespace dewarping {
struct TextLineTracerTestAccess {
  static void findMidLineSeeds(const imageproc::SEDM& sedm, const QLineF& midLine, std::vector<QPoint>& seeds) {
    TextLineTracer::findMidLineSeeds(sedm, midLine, seeds);
  }
};

namespace tests {
namespace {
const double kNaN = std::numeric_limits<double>::quiet_NaN();

QImage uniformGray(const QSize& size, const uint8_t level) {
  imageproc::GrayImage image(size);
  image.fill(level);
  return image.toQImage();
}

class NoCancelStatus : public TaskStatus {
 public:
  void cancel() override {}
  bool isCancelled() const override { return false; }
  void throwIfCancelled() const override {}
};

const QSize kPageSize(1000, 1500);  // A page at 200 DPI, the resolution the tracer works at.
const int kTextLeft = 100;
const int kTextRight = 900;
const int kLineStep = 34;
// The tracer works on a blurred page, so its lines end a little off the ink.
const double kLineEndOffset = 15.0;

QImage blankPage() {
  QImage page(kPageSize, QImage::Format_Grayscale8);
  page.fill(Qt::white);
  return page;
}

/** A line of text: dark letters, grouped into words, from \p left to exactly \p right. */
void drawTextLine(QImage& page, const int left, const int right, const int y) {
  const int letterWidth = 7;
  const int letterHeight = 16;
  QPainter painter(&page);
  int lettersInWord = 0;
  for (int x = left; x + letterWidth < right; x += 11) {
    painter.fillRect(x, y, letterWidth, letterHeight, Qt::black);
    if (++lettersInWord == 6) {
      x += 8;
      lettersInWord = 0;
    }
  }
  painter.fillRect(right - letterWidth, y, letterWidth, letterHeight, Qt::black);
}

void drawTextLines(QImage& page, const int count, const int firstY, const int left = kTextLeft) {
  for (int i = 0; i < count; ++i) {
    drawTextLine(page, left, kTextRight, firstY + i * kLineStep);
  }
}

DistortionModelBuilder traceLines(const QImage& page) {
  DistortionModelBuilder builder(Vec2d(0, 1));
  TextLineTracer::trace(imageproc::GrayImage(page), Dpi(200, 200), page.rect(), builder, NoCancelStatus());
  return builder;
}

double tiltInDegrees(const QLineF& line) {
  return std::atan2(line.dx(), line.dy()) / constants::DEG2RAD;
}

double xAtY(const QLineF& line, const double y) {
  return line.x1() + line.dx() * (y - line.y1()) / line.dy();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DewarpingTestSuite)

BOOST_AUTO_TEST_CASE(test_curve_with_non_finite_points_is_invalid) {
  // A damaged project file can hold NaN or infinity anywhere in a curve.
  BOOST_TEST(Curve(std::vector<QPointF>{{0, 0}, {50, 1}, {100, 0}}).isValid());
  BOOST_TEST(!Curve(std::vector<QPointF>{{0, 0}, {kNaN, kNaN}, {100, 0}}).isValid());
  BOOST_TEST(!Curve(std::vector<QPointF>{{0, 0}, {std::numeric_limits<double>::infinity(), 1}, {100, 0}}).isValid());

  DistortionModel model;
  model.setTopCurve(Curve(std::vector<QPointF>{{0, 0}, {kNaN, kNaN}, {1500, 0}}));
  model.setBottomCurve(Curve(std::vector<QPointF>{{0, 1500}, {750, 1500}, {1500, 1500}}));
  BOOST_TEST(!model.isValid());
}

BOOST_AUTO_TEST_CASE(test_dewarping_with_nan_grid_points) {
  // NaN passed the overflow guard and ended in an integer division by zero.
  const std::vector<QPointF> top{{0, 0}, {kNaN, kNaN}, {1500, 0}};
  const std::vector<QPointF> bottom{{0, 1500}, {750, 1500}, {1500, 1500}};
  const CylindricalSurfaceDewarper dewarper(top, bottom, 2.0);
  const QImage result = RasterDewarper::dewarp(uniformGray(QSize(1500, 1500), 200), QSize(8, 8), dewarper,
                                               QRectF(0, 0, 8, 8), QColor(Qt::white));
  BOOST_CHECK(result.size() == QSize(8, 8));
}

BOOST_AUTO_TEST_CASE(test_large_footprint_keeps_the_gray_level) {
  // Each output pixel covers 500 x 500 source pixels; the 32 bit weights overflowed.
  const std::vector<QPointF> top{{0, 0}, {1000, 0}, {2000, 0}};
  const std::vector<QPointF> bottom{{0, 2000}, {1000, 2000}, {2000, 2000}};
  const CylindricalSurfaceDewarper dewarper(top, bottom, 2.0);
  const QImage result = RasterDewarper::dewarp(uniformGray(QSize(2000, 2000), 200), QSize(4, 4), dewarper,
                                               QRectF(0, 0, 4, 4), QColor(Qt::white));
  BOOST_REQUIRE(result.size() == QSize(4, 4));
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      BOOST_TEST(std::abs(qGray(result.pixel(x, y)) - 200) <= 1);
    }
  }
}

BOOST_AUTO_TEST_CASE(test_footprint_beyond_two_million_pixels_keeps_the_gray_level) {
  // The single output pixel covers 2000 x 2000 source pixels. In 1/32 pixel units that area is
  // 4.1e9 and was computed in int before widening, which overflows above 2^21 source pixels.
  const std::vector<QPointF> top{{0, 0}, {1000, 0}, {2000, 0}};
  const std::vector<QPointF> bottom{{0, 2000}, {1000, 2000}, {2000, 2000}};
  const CylindricalSurfaceDewarper dewarper(top, bottom, 2.0);
  const QImage result = RasterDewarper::dewarp(uniformGray(QSize(2000, 2000), 200), QSize(1, 1), dewarper,
                                               QRectF(0, 0, 1, 1), QColor(Qt::white));
  BOOST_REQUIRE(result.size() == QSize(1, 1));
  BOOST_TEST(std::abs(qGray(result.pixel(0, 0)) - 200) <= 1);
}

BOOST_AUTO_TEST_CASE(test_mid_line_outside_the_image) {
  // A mid line that misses the image can't be clipped; it was traversed as it was, reading the
  // distance map outside its memory.
  const imageproc::SEDM sedm(imageproc::BinaryImage(QSize(100, 80), imageproc::WHITE));
  std::vector<QPoint> seeds;
  TextLineTracerTestAccess::findMidLineSeeds(sedm, QLineF(-100000, -100000, -100000, 100000), seeds);
  BOOST_TEST(seeds.empty());
}

// The dewarper needs two points on each directrix; fewer used to be read as if they were there.
BOOST_AUTO_TEST_CASE(test_directrix_with_fewer_than_two_points_is_rejected) {
  const std::vector<QPointF> good{{0, 0}, {1000, 0}};
  const std::vector<QPointF> single{{0, 2000}};
  BOOST_CHECK_THROW(CylindricalSurfaceDewarper(good, single, 2.0), std::invalid_argument);
  BOOST_CHECK_THROW(CylindricalSurfaceDewarper(single, good, 2.0), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(test_full_page_of_text_keeps_a_model) {
  QImage page = blankPage();
  drawTextLines(page, 38, 100);
  const DistortionModelBuilder builder = traceLines(page);
  BOOST_TEST(builder.tryBuildModel().isValid());
}

BOOST_AUTO_TEST_CASE(test_page_number_does_not_tilt_the_right_edge) {
  // Text on the upper part only: the right edge used to run from the text to the page number.
  QImage page = blankPage();
  drawTextLines(page, 16, 100);
  QPainter(&page).fillRect(480, 1420, 40, 16, Qt::black);

  const DistortionModelBuilder builder = traceLines(page);
  BOOST_TEST(builder.tryBuildModel().isValid());
  const QLineF rightEdge = builder.verticalBounds().second;
  BOOST_TEST(std::fabs(tiltInDegrees(rightEdge)) < 0.5);
  BOOST_TEST(std::fabs(xAtY(rightEdge, 1400) - kTextRight) < kLineEndOffset);
}

BOOST_AUTO_TEST_CASE(test_indented_block_does_not_tilt_the_left_edge) {
  // Indented lines at the bottom used to pull the left edge inwards.
  QImage page = blankPage();
  drawTextLines(page, 14, 100);
  drawTextLines(page, 26, 100 + 14 * kLineStep, kTextLeft + 80);

  const DistortionModelBuilder builder = traceLines(page);
  BOOST_TEST(builder.tryBuildModel().isValid());
  const QLineF leftEdge = builder.verticalBounds().first;
  BOOST_TEST(std::fabs(tiltInDegrees(leftEdge)) < 0.5);
  BOOST_TEST(std::fabs(xAtY(leftEdge, 1400) - kTextLeft) < kLineEndOffset);
}

BOOST_AUTO_TEST_CASE(test_too_few_lines_give_no_model) {
  QImage page = blankPage();
  drawTextLines(page, 2, 100);
  BOOST_TEST(!traceLines(page).tryBuildModel().isValid());
}

BOOST_AUTO_TEST_CASE(test_short_text_block_gives_no_model) {
  // The lines span too little of the page: small errors would grow towards the page's other end.
  QImage page = blankPage();
  drawTextLines(page, 8, 100);
  BOOST_TEST(!traceLines(page).tryBuildModel().isValid());
}

BOOST_AUTO_TEST_CASE(test_lines_on_part_of_the_text_give_no_model) {
  // Two narrow columns above leave no traced lines there, so the model would be extrapolated over them.
  QImage page = blankPage();
  for (int i = 0; i < 18; ++i) {
    const int y = 100 + i * kLineStep;
    drawTextLine(page, kTextLeft, 280, y);
    drawTextLine(page, 560, 740, y);
  }
  drawTextLines(page, 12, 100 + 18 * kLineStep);
  BOOST_TEST(!traceLines(page).tryBuildModel().isValid());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace dewarping
