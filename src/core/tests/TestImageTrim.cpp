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

// Trims that leave less than the minimum side: the part that one edge can't give up is taken
// from the opposite edge. The expected rectangles are those the code produced before the
// excess was redistributed in one place (review FO-06).
BOOST_AUTO_TEST_CASE(inner_rect_takes_the_excess_from_both_edges) {
  struct Case {
    int leading;
    int trailing;
    int imageSide;
    int expectedPos;
    int expectedLength;
  };
  const Case cases[] = {
      {100, 0, 100, 68, 32},  // Only the leading edge can give up something.
      {0, 100, 100, 0, 32},   // Only the trailing edge can.
      {60, 60, 100, 34, 32},  // Both edges give up the same.
      {10, 70, 100, 4, 32},   // Both can, unequal trims.
      {3, 90, 100, 0, 32},    // The leading edge gives up all it has.
      {2, 0, 20, 0, 20},      // Image smaller than the minimum side.
  };
  for (const Case& c : cases) {
    ImageTrim horizontal;
    horizontal.enabled = true;
    horizontal.left = c.leading;
    horizontal.right = c.trailing;
    const QRect h = horizontal.toInnerRect(QSize(c.imageSide, 200));
    BOOST_CHECK_EQUAL(h.x(), c.expectedPos);
    BOOST_CHECK_EQUAL(h.width(), c.expectedLength);

    ImageTrim vertical;
    vertical.enabled = true;
    vertical.top = c.leading;
    vertical.bottom = c.trailing;
    const QRect v = vertical.toInnerRect(QSize(200, c.imageSide));
    BOOST_CHECK_EQUAL(v.y(), c.expectedPos);
    BOOST_CHECK_EQUAL(v.height(), c.expectedLength);
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
