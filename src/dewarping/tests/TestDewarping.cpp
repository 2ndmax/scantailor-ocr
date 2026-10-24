// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <Curve.h>
#include <CylindricalSurfaceDewarper.h>
#include <DistortionModel.h>
#include <GrayImage.h>
#include <RasterDewarper.h>
#include <SEDM.h>
#include <TextLineTracer.h>

#include <QColor>
#include <QImage>
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include <limits>
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

BOOST_AUTO_TEST_CASE(test_mid_line_outside_the_image) {
  // A mid line that misses the image can't be clipped; it was traversed as it was, reading the
  // distance map outside its memory.
  const imageproc::SEDM sedm(imageproc::BinaryImage(QSize(100, 80), imageproc::WHITE));
  std::vector<QPoint> seeds;
  TextLineTracerTestAccess::findMidLineSeeds(sedm, QLineF(-100000, -100000, -100000, 100000), seeds);
  BOOST_TEST(seeds.empty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace dewarping
