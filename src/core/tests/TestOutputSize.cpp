// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Dpi.h>
#include <ImageTransformation.h>
#include <Margins.h>
#include <filters/page_layout/Alignment.h>
#include <filters/page_layout/OutputSizeOptions.h>
#include <filters/page_layout/Params.h>
#include <filters/page_layout/Utils.h>

#include <QDomDocument>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace page_layout;

namespace {
// Content 100 x 150 mm with 10 mm margins all around: 120 x 170 mm.
const QSizeF CONTENT_MM(100, 150);
const Margins MARGINS_MM(10, 10, 10, 10);

Alignment filling() {
  return Alignment(Alignment::VCENTER, Alignment::HCENTER);
}

Alignment scaling() {
  Alignment alignment(Alignment::VCENTER, Alignment::HCENTER);
  alignment.setScaleToFit(true);
  return alignment;
}

OutputSizeOptions ownTarget(const QSizeF& sizeMm) {
  OutputSizeOptions options;
  options.setOwnTargetSize(true);
  options.setTargetSizeMM(sizeMm);
  return options;
}

double factor(const Alignment& alignment, const QSizeF& commonMm, const OutputSizeOptions& options) {
  return Utils::calcScaleFactor(CONTENT_MM, MARGINS_MM, alignment, commonMm, options);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OutputSizeTestSuite)

BOOST_AUTO_TEST_CASE(pages_not_matched_are_never_scaled) {
  Alignment off;
  off.setNull(true);
  BOOST_CHECK_EQUAL(factor(off, QSizeF(300, 400), OutputSizeOptions()), 1.0);
  BOOST_CHECK_EQUAL(factor(off, QSizeF(60, 80), ownTarget(QSizeF(60, 80))), 1.0);
}

BOOST_AUTO_TEST_CASE(filling_without_own_target_is_as_before) {
  // The common size is never smaller than a matched page; and even if it were, nothing is scaled.
  BOOST_CHECK_EQUAL(factor(filling(), QSizeF(200, 300), OutputSizeOptions()), 1.0);
  BOOST_CHECK_EQUAL(factor(filling(), QSizeF(110, 160), OutputSizeOptions()), 1.0);
}

BOOST_AUTO_TEST_CASE(filling_reduces_to_a_smaller_own_target_only) {
  BOOST_CHECK_EQUAL(factor(filling(), QSizeF(120, 170), ownTarget(QSizeF(200, 300))), 1.0);
  // 90 / 100 and 140 / 150: the smaller one.
  BOOST_CHECK_CLOSE(factor(filling(), QSizeF(120, 170), ownTarget(QSizeF(110, 160))), 0.9, 1e-9);
  // Not below the limit (50 %).
  BOOST_CHECK_CLOSE(factor(filling(), QSizeF(120, 170), ownTarget(QSizeF(40, 60))), 0.5, 1e-9);
}

BOOST_AUTO_TEST_CASE(scaling_enlarges_and_reduces_within_the_limits) {
  // 140 / 100 and 210 / 150.
  BOOST_CHECK_CLOSE(factor(scaling(), QSizeF(160, 230), OutputSizeOptions()), 1.4, 1e-9);
  // 200 / 100 and 300 / 150: limited to 150 %.
  BOOST_CHECK_CLOSE(factor(scaling(), QSizeF(220, 320), OutputSizeOptions()), 1.5, 1e-9);
  BOOST_CHECK_CLOSE(factor(scaling(), QSizeF(120, 170), ownTarget(QSizeF(110, 160))), 0.9, 1e-9);

  OutputSizeOptions limits;
  limits.setMaxEnlargePercent(120);
  BOOST_CHECK_CLOSE(factor(scaling(), QSizeF(160, 230), limits), 1.2, 1e-9);
}

BOOST_AUTO_TEST_CASE(the_largest_page_is_not_scaled_by_a_rounding_error) {
  BOOST_CHECK_EQUAL(factor(scaling(), QSizeF(120 + 1e-9, 170), OutputSizeOptions()), 1.0);
}

BOOST_AUTO_TEST_CASE(scaled_page_has_the_target_size_and_unchanged_margins) {
  // 254 dpi: 10 pixels per millimeter.
  const Dpi dpi(254, 254);
  const ImageTransformation xform(QRectF(0, 0, 3000, 4000), dpi);
  const QPolygonF contentRectPhys(QRectF(500, 500, 1000, 1500));
  const Params params(MARGINS_MM, QRectF(), QRectF(), CONTENT_MM, scaling(), false);

  double scale = 0;
  const QPolygonF pageRectPhys(
      Utils::calcPageRectPhys(xform, contentRectPhys, params, QSizeF(160, 230), OutputSizeOptions(), &scale));
  BOOST_REQUIRE_CLOSE(scale, 1.4, 1e-9);

  ImageTransformation newXform(xform);
  newXform.setPostCropArea(newXform.transform().map(pageRectPhys));
  newXform.setPostScaleFactor(scale);
  newXform.postScaleToDpi(dpi);

  // The output page is 160 x 230 mm, the content 140 x 210 mm: the margins stay 10 mm.
  BOOST_CHECK_CLOSE(newXform.resultingRect().width(), 1600.0, 1e-6);
  BOOST_CHECK_CLOSE(newXform.resultingRect().height(), 2300.0, 1e-6);
  const QRectF contentOut(newXform.transform().map(contentRectPhys).boundingRect());
  BOOST_CHECK_CLOSE(contentOut.width(), 1400.0, 1e-6);
  BOOST_CHECK_CLOSE(contentOut.left() - newXform.resultingRect().left(), 100.0, 1e-6);
}

BOOST_AUTO_TEST_CASE(options_and_alignment_are_stored_only_when_used) {
  QDomDocument doc;
  BOOST_CHECK(OutputSizeOptions().isDefault());
  BOOST_CHECK(!filling().toXml(doc, "alignment").hasAttribute("scale"));

  OutputSizeOptions options = ownTarget(QSizeF(148.5, 210));
  options.setMaxEnlargePercent(130);
  options.setMinReducePercent(70);
  BOOST_CHECK(OutputSizeOptions(options.toXml(doc, "output-size")) == options);
  BOOST_CHECK(OutputSizeOptions(QDomElement()) == OutputSizeOptions());

  const Alignment restored(scaling().toXml(doc, "alignment"));
  BOOST_CHECK(restored == scaling());
  BOOST_CHECK(restored.isScaleToFit());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
