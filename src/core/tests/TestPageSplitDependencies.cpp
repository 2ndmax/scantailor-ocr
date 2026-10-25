// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AutoManualMode.h>
#include <OrthogonalRotation.h>
#include <filters/page_split/Dependencies.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/Params.h>

#include <QDomDocument>
#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace page_split;

namespace {
const QSize kImageSize(2000, 1500);

// The whole image, i.e. no trim.
const QPolygonF kUntrimmed(QRectF(0, 0, 2000, 1500));

// 300 px trimmed off on the left.
const QPolygonF kTrimmedLeft(QRectF(300, 0, 1700, 1500));

Params paramsWith(const Dependencies& deps) {
  const PageLayout layout(QRectF(0, 0, 2000, 1500), QLineF(1000, 0, 1000, 1500));
  return Params(layout, deps, MODE_AUTO);
}

Dependencies throughXml(const Dependencies& deps) {
  QDomDocument doc;
  return Dependencies(deps.toXml(doc, "dependencies"));
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PageSplitDependenciesTestSuite)

BOOST_AUTO_TEST_CASE(trim_change_invalidates_split) {
  // The split line is stored relative to the trimmed area, so it would be off by the trim.
  const Dependencies stored(kImageSize, OrthogonalRotation(), kUntrimmed, TWO_PAGES);
  const Dependencies current(kImageSize, OrthogonalRotation(), kTrimmedLeft, TWO_PAGES);
  BOOST_CHECK(!current.compatibleWith(paramsWith(stored)));
  BOOST_CHECK(stored.compatibleWith(paramsWith(stored)));
}

BOOST_AUTO_TEST_CASE(trim_survives_project_file) {
  const Dependencies stored(throughXml(Dependencies(kImageSize, OrthogonalRotation(), kTrimmedLeft, TWO_PAGES)));
  BOOST_CHECK(stored.hasPreCropArea());
  BOOST_CHECK(
      Dependencies(kImageSize, OrthogonalRotation(), kTrimmedLeft, TWO_PAGES).compatibleWith(paramsWith(stored)));
  BOOST_CHECK(
      !Dependencies(kImageSize, OrthogonalRotation(), kUntrimmed, TWO_PAGES).compatibleWith(paramsWith(stored)));
}

BOOST_AUTO_TEST_CASE(old_project_keeps_split_line) {
  // Project files written before the trim was a dependency keep their split line (decision 5).
  const Dependencies stored(throughXml(Dependencies(kImageSize, OrthogonalRotation(), QPolygonF(), TWO_PAGES)));
  BOOST_CHECK(!stored.hasPreCropArea());
  BOOST_CHECK(
      Dependencies(kImageSize, OrthogonalRotation(), kTrimmedLeft, TWO_PAGES).compatibleWith(paramsWith(stored)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
