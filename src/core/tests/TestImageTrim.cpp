// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/fix_orientation/ImageTrim.h>

#include <QRect>
#include <QSize>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace fix_orientation;

BOOST_AUTO_TEST_SUITE(ImageTrimTestSuite)

BOOST_AUTO_TEST_CASE(inner_rect_keeps_the_minimum_side) {
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 60;
  trim.right = 60;
  const QRect inner = trim.toInnerRect(QSize(100, 100));
  BOOST_CHECK_EQUAL(inner.width(), kMinTrimInnerSide);
  BOOST_CHECK(QRect(0, 0, 100, 100).contains(inner));
}

// An image smaller than the minimum side: the inner rectangle may not reach beyond it.
BOOST_AUTO_TEST_CASE(inner_rect_of_a_tiny_image_stays_inside) {
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 2;
  trim.top = 2;
  const QSize size(20, 10);
  const QRect inner = trim.toInnerRect(size);
  BOOST_CHECK_MESSAGE(QRect(QPoint(0, 0), size).contains(inner),
                      inner.x() << "," << inner.y() << " " << inner.width() << "x" << inner.height());
  BOOST_CHECK(!inner.isEmpty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
