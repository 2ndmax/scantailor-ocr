// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <SkewFinder.h>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <random>
#include <stdexcept>

namespace imageproc {
namespace tests {
BOOST_AUTO_TEST_SUITE(SkewFinderTestSuite)

BOOST_AUTO_TEST_CASE(test_positive_detection) {
  QImage image(1000, 800, QImage::Format_ARGB32_Premultiplied);
  image.fill(0xffffffff);
  {
    // Draw parallel strokes in "skewed page" space (no QTextDocument / fonts).
    // Headless RPM/mock chroots often have no fonts; QPainter::drawText() can abort there.
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(QPen(QColor(0, 0, 0), 2));
    QTransform xform1;
    xform1.translate(-0.5 * image.width(), -0.5 * image.height());
    QTransform xform2;
    xform2.rotate(4.5);
    QTransform xform3;
    xform3.translate(0.5 * image.width(), 0.5 * image.height());
    painter.setWorldTransform(xform1 * xform2 * xform3);
    for (int y = -500; y <= 500; y += 14) {
      painter.drawLine(QLineF(-700, y, 700, y));
    }
  }

  SkewFinder skewFinder;
  const Skew skew(skewFinder.findSkew(BinaryImage(image)));
  BOOST_REQUIRE(std::fabs(skew.angle() - 4.5) < 0.15);
  BOOST_CHECK(skew.confidence() >= Skew::GOOD_CONFIDENCE);
}

BOOST_AUTO_TEST_CASE(test_negative_detection) {
  QImage image(1000, 800, QImage::Format_Mono);
  image.setColorCount(2);
  image.setColor(0, 0xff000000);
  image.setColor(1, 0xffffffff);
  image.fill(1);

  std::mt19937 rng(0x5CA1E005u);
  const int numDots = image.width() * image.height() / 5;
  for (int i = 0; i < numDots; ++i) {
    const int x = static_cast<int>(rng() % static_cast<unsigned>(image.width()));
    const int y = static_cast<int>(rng() % static_cast<unsigned>(image.height()));
    image.setPixel(x, y, 0);
  }

  SkewFinder skewFinder;
  skewFinder.setCoarseReduction(0);
  skewFinder.setFineReduction(0);
  const Skew skew(skewFinder.findSkew(BinaryImage(image)));
  BOOST_CHECK(skew.confidence() < Skew::GOOD_CONFIDENCE);
}

// A DPI of 0 on both axes gives 0 / 0. NaN passed the check "ratio <= 0", and the shear built from it
// ended in an undefined cast to int.
BOOST_AUTO_TEST_CASE(test_non_finite_resolution_ratio_is_refused) {
  SkewFinder skewFinder;
  for (const double ratio : {std::nan(""), HUGE_VAL, 0.0, -1.0}) {
    BOOST_CHECK_THROW(skewFinder.setResolutionRatio(ratio), std::invalid_argument);
  }
  BOOST_CHECK_NO_THROW(skewFinder.setResolutionRatio(0.5));
}

BOOST_AUTO_TEST_CASE(test_parameter_validation_and_search_bounds) {
  SkewFinder finder;
  for (const double bad : {std::nan(""), HUGE_VAL, -1.0, 46.0}) {
    BOOST_CHECK_THROW(finder.setMaxAngle(bad), std::invalid_argument);
  }
  for (const double bad : {std::nan(""), HUGE_VAL, -0.1, SkewFinder::DEFAULT_MAX_ANGLE + 1.0}) {
    BOOST_CHECK_THROW(finder.setMinAngle(bad), std::invalid_argument);
  }
  for (const double bad : {std::nan(""), HUGE_VAL, 0.0, -0.1}) {
    BOOST_CHECK_THROW(finder.setDesiredAccuracy(bad), std::invalid_argument);
  }

  // Blank image with coarse accuracy >= 1.0 must produce finite non-negative confidence (no 0/0 NaN).
  SkewFinder coarseFinder;
  coarseFinder.setDesiredAccuracy(2.0);
  const BinaryImage blank(200, 200, WHITE);
  const Skew blankSkew = coarseFinder.findSkew(blank);
  BOOST_CHECK(std::isfinite(blankSkew.angle()));
  BOOST_CHECK(std::isfinite(blankSkew.confidence()));
  BOOST_CHECK_GE(blankSkew.confidence(), 0.0);

  // Fine search must remain clamped within [-maxAngle, maxAngle].
  BinaryImage skewed(400, 300, WHITE);
  for (int x = 0; x < 400; ++x) {
    for (int offset = 40; offset <= 220; offset += 20) {
      const int lineY = offset + static_cast<int>(std::lround(0.08 * x));  // ~4.6 deg
      for (int dy = -1; dy <= 1; ++dy) {
        const int y = lineY + dy;
        if ((y >= 0) && (y < 300)) {
          skewed.setPixel(x, y, BLACK);
        }
      }
    }
  }
  SkewFinder clampedFinder;
  clampedFinder.setMaxAngle(2.0);
  clampedFinder.setCoarseReduction(0);
  clampedFinder.setFineReduction(0);
  const Skew clampedSkew = clampedFinder.findSkew(skewed);
  BOOST_CHECK_LE(std::fabs(clampedSkew.angle()), 2.0 + 1e-6);

  // findSkewFromTopEdge must apply resolutionRatio consistently.
  BinaryImage topEdge(300, 300, WHITE);
  for (int x = 0; x < 300; ++x) {
    const int edgeY = 30 + static_cast<int>(std::lround(0.05 * x));
    for (int y = 0; y < edgeY; ++y) {
      topEdge.setPixel(x, y, BLACK);
    }
  }
  SkewFinder edgeFinder;
  const Skew s1 = edgeFinder.findSkewFromTopEdge(topEdge);
  edgeFinder.setResolutionRatio(2.0);
  const Skew s2 = edgeFinder.findSkewFromTopEdge(topEdge);
  BOOST_CHECK(s1.confidence() >= Skew::GOOD_CONFIDENCE);
  BOOST_CHECK(s2.confidence() >= Skew::GOOD_CONFIDENCE);
  BOOST_CHECK_CLOSE(s2.angle() / s1.angle(), 2.0, 5.0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc