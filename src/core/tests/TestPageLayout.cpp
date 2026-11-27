// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/page_split/PageLayout.h>
#include <filters/page_split/PageLayoutAdapter.h>

#include <QDomDocument>
#include <QLineF>
#include <QRectF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace page_split;

BOOST_AUTO_TEST_SUITE(PageLayoutTestSuite)

// With two pages both cutters are the same line, so a change to single cut pages has two lines.
BOOST_AUTO_TEST_CASE(test_two_pages_have_both_cutters) {
  const QLineF split(40, 0, 40, 200);
  PageLayout layout(QRectF(0, 0, 100, 200), split);
  layout.setType(PageLayout::SINGLE_PAGE_CUT);
  BOOST_CHECK(layout.cutterLine(0) == split);
  BOOST_CHECK(layout.cutterLine(1) == split);

  // The same after the project file was read, which stores only the first line for two pages.
  QDomDocument doc;
  const PageLayout original(QRectF(0, 0, 100, 200), split);
  PageLayout loaded(original.toXml(doc, "layout"));
  BOOST_REQUIRE(loaded.type() == PageLayout::TWO_PAGES);
  loaded.setType(PageLayout::SINGLE_PAGE_CUT);
  BOOST_CHECK(loaded.cutterLine(0) == split);
  BOOST_CHECK(loaded.cutterLine(1) == split);
}

// Lines that can't be adapted keep their points as they are. They are put in order, so that the
// sorting by the upper point finds the left line first.
BOOST_AUTO_TEST_CASE(test_cutters_are_sorted_by_their_upper_points) {
  const PageLayout layout(QRectF(0, 0, 200, 100), QLineF(60, 90, 40, 10), QLineF(55, 10, 55, 90));
  BOOST_REQUIRE(layout.type() == PageLayout::SINGLE_PAGE_CUT);

  // An empty rectangle can't be adapted to.
  const PageLayout adapted = PageLayoutAdapter::adaptPageLayout(layout, QRectF());
  BOOST_REQUIRE(adapted.type() == PageLayout::SINGLE_PAGE_CUT);
  BOOST_CHECK(adapted.cutterLine(0) == QLineF(40, 10, 60, 90));
  BOOST_CHECK(adapted.cutterLine(1) == QLineF(55, 10, 55, 90));
}

BOOST_AUTO_TEST_CASE(correct_page_layout_type_preserves_subpixel_parallel_cutters) {
  PageLayout layout(QRectF(0, 0, 1000, 1000), QLineF(100.4, 0.0, 100.6, 2.0), QLineF(900.4, 0.0, 900.6, 2.0));
  BOOST_REQUIRE(layout.type() == PageLayout::SINGLE_PAGE_CUT);

  PageLayoutAdapter::correctPageLayoutType(&layout);
  BOOST_CHECK(layout.type() == PageLayout::SINGLE_PAGE_CUT);
}

BOOST_AUTO_TEST_CASE(correct_page_layout_type_downgrades_collinear_and_null_cutters) {
  PageLayout collinear(QRectF(0, 0, 1000, 1000), QLineF(100.0, 0.0, 100.0, 2.0), QLineF(100.0, 5.0, 100.0, 7.0));
  PageLayoutAdapter::correctPageLayoutType(&collinear);
  BOOST_CHECK(collinear.type() == PageLayout::SINGLE_PAGE_UNCUT);

  PageLayout nullCut(QRectF(0, 0, 1000, 1000), QLineF(), QLineF(900.0, 0.0, 900.0, 1000.0));
  nullCut.setType(PageLayout::SINGLE_PAGE_CUT);
  PageLayoutAdapter::correctPageLayoutType(&nullCut);
  BOOST_CHECK(nullCut.type() == PageLayout::SINGLE_PAGE_UNCUT);

  PageLayout nullTwoPages(QRectF(0, 0, 1000, 1000), QLineF());
  nullTwoPages.setType(PageLayout::TWO_PAGES);
  PageLayoutAdapter::correctPageLayoutType(&nullTwoPages);
  BOOST_CHECK(nullTwoPages.type() == PageLayout::SINGLE_PAGE_UNCUT);
}

BOOST_AUTO_TEST_CASE(correct_page_layout_type_respects_allow_full_page_cut) {
  const QRectF outline(0, 0, 100, 200);
  PageLayout layout(outline, QLineF(0, 0, 0, 200), QLineF(100, 0, 100, 200));
  BOOST_REQUIRE(layout.type() == PageLayout::SINGLE_PAGE_CUT);

  PageLayoutAdapter::correctPageLayoutType(&layout, true);
  BOOST_CHECK(layout.type() == PageLayout::SINGLE_PAGE_CUT);

  PageLayoutAdapter::correctPageLayoutType(&layout, false);
  BOOST_CHECK(layout.type() == PageLayout::SINGLE_PAGE_UNCUT);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
