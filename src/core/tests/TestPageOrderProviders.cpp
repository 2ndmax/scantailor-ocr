// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageId.h>
#include <Margins.h>
#include <PageId.h>
#include <filters/page_layout/Alignment.h>
#include <filters/page_layout/OrderBySizeProvider.h>
#include <filters/page_layout/Params.h>
#include <filters/page_layout/Settings.h>

#include <QRectF>
#include <QSizeF>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace Tests {
namespace {
page_layout::Params layoutParams(const QSizeF& contentSizeMm) {
  return page_layout::Params(Margins(10, 10, 10, 10), QRectF(0, 0, 100, 100), QRectF(10, 10, 80, 80), contentSizeMm,
                             page_layout::Alignment(), false);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PageOrderProvidersTestSuite)

BOOST_AUTO_TEST_CASE(test_unknown_content_size_goes_to_the_back) {
  // An unknown content size (-1 x -1) plus the margins looked like a narrow, known size.
  const auto settings = std::make_shared<page_layout::Settings>();
  const PageId unknown(ImageId("/in/unknown.png"));
  const PageId known(ImageId("/in/known.png"));
  settings->setPageParams(unknown, layoutParams(QSizeF()));
  settings->setPageParams(known, layoutParams(QSizeF(100, 150)));

  for (const auto dimension : {page_layout::OrderBySizeProvider::WIDTH, page_layout::OrderBySizeProvider::HEIGHT}) {
    const page_layout::OrderBySizeProvider order(settings, dimension);
    BOOST_TEST(order.precedes(known, false, unknown, false));
    BOOST_TEST(!order.precedes(unknown, false, known, false));
  }
}

BOOST_AUTO_TEST_CASE(test_snapshot_keeps_the_values_while_sorting) {
  // A processing thread may change the settings while the thumbnails are sorted; the comparator
  // compares the values it read at the start, so the order stays consistent.
  const auto settings = std::make_shared<page_layout::Settings>();
  const PageId narrow(ImageId("/in/narrow.png"));
  const PageId wide(ImageId("/in/wide.png"));
  settings->setPageParams(narrow, layoutParams(QSizeF(50, 50)));
  settings->setPageParams(wide, layoutParams(QSizeF(200, 200)));

  const auto order
      = std::make_shared<page_layout::OrderBySizeProvider>(settings, page_layout::OrderBySizeProvider::WIDTH);
  const PageOrderProvider::Comparator precedes = order->snapshot({narrow, wide});
  const PageOrderProvider::Comparator reversed = order->reversed()->snapshot({narrow, wide});
  settings->setPageParams(narrow, layoutParams(QSizeF(300, 300)));

  BOOST_TEST(precedes(narrow, false, wide, false));
  BOOST_TEST(!precedes(wide, false, narrow, false));
  BOOST_TEST(reversed(wide, false, narrow, false));
  // precedes() itself reads the current settings.
  BOOST_TEST(order->precedes(wide, false, narrow, false));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
