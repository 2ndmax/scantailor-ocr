// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ScanInsertion.h>

#include <QStringList>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(ScanInsertionTestSuite)

BOOST_AUTO_TEST_CASE(test_prefixed_file_name) {
  using scan_insertion::prefixedFileName;
  BOOST_CHECK(prefixedFileName("/scans/2026-09-29-09-49-52-01.tif", "/scans/2026-10-07-10-15-03-01.tif")
              == "2026-09-29-09-49-52-01_2026-10-07-10-15-03-01.tif");
  // An anchor that was inserted itself makes the name grow.
  BOOST_CHECK(prefixedFileName("/scans/A_B.tif", "/scans/C.tif") == "A_B_C.tif");
  // Only the last extension of the anchor is dropped; the scan keeps its own.
  BOOST_CHECK(prefixedFileName("/scans/page.1.png", "/scans/D.tiff") == "page.1_D.tiff");
}

BOOST_AUTO_TEST_CASE(test_names_sort_after_the_anchor) {
  using scan_insertion::prefixedFileName;
  QStringList names{"A.tif", "B.tif", prefixedFileName("A.tif", "C.tif"), prefixedFileName("A.tif", "D.tif")};
  names.sort();
  BOOST_CHECK(names == QStringList({"A.tif", "A_C.tif", "A_D.tif", "B.tif"}));
}

BOOST_AUTO_TEST_CASE(test_scans_follow_each_other) {
  ScanInsertionAnchor anchor;
  BOOST_CHECK(!anchor.isActive());
  anchor.start("A.tif");
  BOOST_CHECK(anchor.insertAfterFile() == "A.tif");
  anchor.inserted("A_C.tif");
  BOOST_CHECK(anchor.anchorFile() == "A.tif");
  BOOST_CHECK(anchor.insertAfterFile() == "A_C.tif");
  anchor.inserted("A_D.tif");
  BOOST_CHECK(anchor.insertAfterFile() == "A_D.tif");
}

BOOST_AUTO_TEST_CASE(test_selecting_the_anchor_or_last_scan_keeps_the_anchor) {
  ScanInsertionAnchor anchor;
  anchor.start("A.tif");
  anchor.inserted("A_C.tif");
  BOOST_CHECK(!anchor.pageSelected("A_C.tif"));
  BOOST_CHECK(!anchor.pageSelected("A.tif"));
  BOOST_CHECK(anchor.anchorFile() == "A.tif");
  BOOST_CHECK(anchor.insertAfterFile() == "A_C.tif");
}

BOOST_AUTO_TEST_CASE(test_selecting_another_page_makes_it_the_anchor) {
  ScanInsertionAnchor anchor;
  anchor.start("A.tif");
  anchor.inserted("A_C.tif");
  anchor.inserted("A_D.tif");
  // An earlier scan of the series, too.
  BOOST_CHECK(anchor.pageSelected("A_C.tif"));
  BOOST_CHECK(anchor.anchorFile() == "A_C.tif");
  BOOST_CHECK(anchor.insertAfterFile() == "A_C.tif");
  BOOST_CHECK(anchor.pageSelected("B.tif"));
  BOOST_CHECK(anchor.anchorFile() == "B.tif");
}

BOOST_AUTO_TEST_CASE(test_inactive_anchor_ignores_everything) {
  ScanInsertionAnchor anchor;
  anchor.inserted("A_C.tif");
  BOOST_CHECK(!anchor.pageSelected("B.tif"));
  BOOST_CHECK(!anchor.isActive());
  anchor.start("A.tif");
  anchor.clear();
  BOOST_CHECK(!anchor.isActive());
  BOOST_CHECK(anchor.insertAfterFile().isEmpty());
}

BOOST_AUTO_TEST_SUITE_END()
