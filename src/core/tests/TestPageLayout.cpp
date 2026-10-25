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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
