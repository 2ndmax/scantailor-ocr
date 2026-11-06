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

// Edges from a damaged project file can be as large as an int holds; adding them overflowed and
// the rectangle ended up outside the image (found by fuzz_project_geometry).
BOOST_AUTO_TEST_CASE(huge_edges_leave_the_rectangle_inside) {
  const QSize size(2480, 3508);
  for (const int left : {2147483647, 120, 0}) {
    ImageTrim trim;
    trim.enabled = true;
    trim.left = left;
    trim.right = 2147483647;
    trim.top = 2147483647;
    trim.bottom = 60;
    const QRect inner = trim.toInnerRect(size);
    BOOST_CHECK_MESSAGE(
        QRect(QPoint(0, 0), size).contains(inner) && !inner.isEmpty(),
        "left " << left << ": " << inner.x() << "," << inner.y() << " " << inner.width() << "x" << inner.height());
  }
}

// "Apply to ..." keeps the length of a trim on a page of another resolution: 30 px at 300 dpi
// are 2.54 mm, which are 60 px at 600 dpi. Each direction uses its own resolution.
BOOST_AUTO_TEST_CASE(rescaled_keeps_the_length_for_another_resolution) {
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 30;
  trim.right = 15;
  trim.top = 10;
  trim.bottom = 1;
  const ImageTrim scaled = trim.rescaled(Dpi(300, 200), Dpi(600, 300));
  BOOST_CHECK(scaled.enabled);
  BOOST_CHECK_EQUAL(scaled.left, 60);
  BOOST_CHECK_EQUAL(scaled.right, 30);
  BOOST_CHECK_EQUAL(scaled.top, 15);
  BOOST_CHECK_EQUAL(scaled.bottom, 2);  // 1.5 rounds up
}

BOOST_AUTO_TEST_CASE(rescaled_keeps_the_pixels_without_a_resolution) {
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 30;
  BOOST_CHECK_EQUAL(trim.rescaled(Dpi(), Dpi(600, 600)).left, 30);
  BOOST_CHECK_EQUAL(trim.rescaled(Dpi(300, 300), Dpi()).left, 30);
}

BOOST_AUTO_TEST_CASE(rescaled_huge_edges_stay_positive) {
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 2147483647;
  BOOST_CHECK_EQUAL(trim.rescaled(Dpi(100, 100), Dpi(1200, 1200)).left, 2147483647);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
