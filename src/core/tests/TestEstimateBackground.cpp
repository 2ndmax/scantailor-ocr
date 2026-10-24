// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <EstimateBackground.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <NullTaskStatus.h>
#include <PolynomialSurface.h>

#include <QPolygonF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace imageproc;

namespace {
GrayImage uniform(const QSize& size) {
  GrayImage image(size);
  image.fill(200);
  return image;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(EstimateBackgroundTestSuite)

BOOST_AUTO_TEST_CASE(test_very_thin_images) {
  // A content area that is just a line, e.g. a separator: scaled to 300 px, the other side
  // became 0 and the framed working image was written outside its memory.
  const PolynomialSurface wide(estimateBackground(uniform(QSize(5000, 15)), QPolygonF(), NullTaskStatus(), nullptr));
  BOOST_CHECK(wide.render(QSize(50, 3)).size() == QSize(50, 3));
  const PolynomialSurface tall(estimateBackground(uniform(QSize(15, 5000)), QPolygonF(), NullTaskStatus(), nullptr));
  BOOST_CHECK(tall.render(QSize(3, 50)).size() == QSize(3, 50));

  // An empty image gives a flat background instead of an exception.
  BOOST_CHECK_NO_THROW(estimateBackground(GrayImage(), QPolygonF(), NullTaskStatus(), nullptr));
}

BOOST_AUTO_TEST_CASE(test_framed_image_without_pixels) {
  BOOST_TEST(createFramedImage(QSize(300, 0), 255, 0).height() == 0);
  BOOST_TEST(createFramedImage(QSize(0, 300), 255, 0).width() == 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
