// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <Dpi.h>
#include <ImageTransformation.h>
#include <filters/deskew/Dependencies.h>
#include <filters/deskew/Params.h>

#include <QDomDocument>
#include <QPolygonF>
#include <QRectF>
#include <QTransform>
#include <QtMath>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace Tests {
using namespace deskew;

BOOST_AUTO_TEST_SUITE(DeskewParamsTestSuite)

BOOST_AUTO_TEST_CASE(params_oblique_roundtrip_xml) {
  const double deskewDeg = 1.5;
  const double obliqueDeg = 2.25;
  const Dependencies deps;
  const Params original(deskewDeg, obliqueDeg, deps, MODE_MANUAL, MODE_MANUAL);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  const Params restored(doc.documentElement());

  BOOST_CHECK_CLOSE(restored.deskewAngle(), deskewDeg, 1e-6);
  BOOST_CHECK_CLOSE(restored.obliqueAngle(), obliqueDeg, 1e-6);
  BOOST_CHECK(restored.mode() == MODE_MANUAL);
  BOOST_CHECK(restored.obliqueMode() == MODE_MANUAL);
}

BOOST_AUTO_TEST_CASE(params_zero_oblique_roundtrip_xml) {
  const double deskewDeg = -0.5;
  const Dependencies deps;
  const Params original(deskewDeg, 0.0, deps, MODE_AUTO, MODE_AUTO);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  const Params restored(doc.documentElement());

  BOOST_CHECK_CLOSE(restored.deskewAngle(), deskewDeg, 1e-6);
  BOOST_CHECK_SMALL(restored.obliqueAngle(), 1e-9);
  BOOST_CHECK(restored.mode() == MODE_AUTO);
  BOOST_CHECK(restored.obliqueMode() == MODE_AUTO);
}

BOOST_AUTO_TEST_CASE(params_independent_oblique_mode_xml) {
  const Dependencies deps;
  const Params original(1.0, 2.0, deps, MODE_MANUAL, MODE_AUTO);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  const Params restored(doc.documentElement());

  BOOST_CHECK(restored.mode() == MODE_MANUAL);
  BOOST_CHECK(restored.obliqueMode() == MODE_AUTO);
}

BOOST_AUTO_TEST_CASE(params_oblique_mode_attribute_roundtrip_xml) {
  const Dependencies deps;
  const Params original(-1.25, 0.75, deps, MODE_AUTO, MODE_MANUAL);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  BOOST_CHECK(el.hasAttribute("oblique-mode"));
  BOOST_CHECK(el.attribute("oblique-mode") == "manual");

  const Params restored(doc.documentElement());
  BOOST_CHECK(restored.mode() == MODE_AUTO);
  BOOST_CHECK(restored.obliqueMode() == MODE_MANUAL);
  BOOST_CHECK_CLOSE(restored.deskewAngle(), -1.25, 1e-6);
  BOOST_CHECK_CLOSE(restored.obliqueAngle(), 0.75, 1e-6);
}

/** Selective Apply To… merge semantics (issue #117): deskew-only must not overwrite oblique. */
BOOST_AUTO_TEST_CASE(apply_merge_deskew_only_preserves_oblique) {
  const Dependencies deps;
  const Params existing(1.0, 3.0, deps, MODE_MANUAL, MODE_MANUAL);
  const Params current(2.0, 4.0, deps, MODE_AUTO, MODE_AUTO);

  const Params merged(current.deskewAngle(), existing.obliqueAngle(), deps, current.mode(), existing.obliqueMode());

  BOOST_CHECK_CLOSE(merged.deskewAngle(), 2.0, 1e-6);
  BOOST_CHECK_CLOSE(merged.obliqueAngle(), 3.0, 1e-6);
  BOOST_CHECK(merged.mode() == MODE_AUTO);
  BOOST_CHECK(merged.obliqueMode() == MODE_MANUAL);
}

/** Selective Apply To… merge semantics (issue #117): oblique-only must not overwrite deskew. */
BOOST_AUTO_TEST_CASE(apply_merge_oblique_only_preserves_deskew) {
  const Dependencies deps;
  const Params existing(1.0, 3.0, deps, MODE_MANUAL, MODE_MANUAL);
  const Params current(2.0, 4.0, deps, MODE_AUTO, MODE_AUTO);

  const Params merged(existing.deskewAngle(), current.obliqueAngle(), deps, existing.mode(), current.obliqueMode());

  BOOST_CHECK_CLOSE(merged.deskewAngle(), 1.0, 1e-6);
  BOOST_CHECK_CLOSE(merged.obliqueAngle(), 4.0, 1e-6);
  BOOST_CHECK(merged.mode() == MODE_MANUAL);
  BOOST_CHECK(merged.obliqueMode() == MODE_AUTO);
}

BOOST_AUTO_TEST_CASE(params_auto_oblique_false_roundtrip_xml) {
  const Dependencies deps;
  const Params original(1.0, 0.25, deps, MODE_AUTO, MODE_MANUAL);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  const Params restored(doc.documentElement());

  BOOST_CHECK(restored.obliqueMode() == MODE_MANUAL);
  BOOST_CHECK(!restored.autoOblique());
  BOOST_CHECK_CLOSE(restored.obliqueAngle(), 0.25, 1e-6);
}

BOOST_AUTO_TEST_CASE(params_missing_autoOblique_attribute_defaults_true) {
  QDomDocument doc;
  QDomElement el(doc.createElement("params"));
  el.setAttribute("mode", "auto");
  el.setAttribute("angle", "0");
  el.setAttribute("oblique", "0");
  const Dependencies deps;
  el.appendChild(deps.toXml(doc, "dependencies"));
  doc.appendChild(el);

  const Params restored(doc.documentElement());
  BOOST_CHECK(restored.autoOblique());
}

BOOST_AUTO_TEST_CASE(default_params_deskew_auto_oblique_off_by_default) {
  const DefaultParams::DeskewParams deskew;
  BOOST_CHECK(!deskew.isAutoOblique());
}

BOOST_AUTO_TEST_CASE(params_detection_roundtrip_xml) {
  const Dependencies deps;
  const Params original(0.5, 0.0, deps, MODE_AUTO, MODE_MANUAL, DETECT_TOP_EDGE);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "params");
  doc.appendChild(el);

  BOOST_CHECK(el.attribute("detection") == "top-edge");
  // The stored method wins over the project-wide one of older projects.
  const Params restored(doc.documentElement(), DETECT_CONTENT);
  BOOST_CHECK(restored.detection() == DETECT_TOP_EDGE);
}

BOOST_AUTO_TEST_CASE(params_missing_detection_uses_legacy_method) {
  QDomDocument doc;
  QDomElement el(doc.createElement("params"));
  el.setAttribute("mode", "auto");
  el.setAttribute("angle", "0");
  const Dependencies deps;
  el.appendChild(deps.toXml(doc, "dependencies"));
  doc.appendChild(el);

  BOOST_CHECK(Params(doc.documentElement()).detection() == DETECT_CONTENT);
  BOOST_CHECK(Params(doc.documentElement(), DETECT_TOP_EDGE).detection() == DETECT_TOP_EDGE);
}

BOOST_AUTO_TEST_CASE(default_params_deskew_detection_roundtrip_xml) {
  DefaultParams::DeskewParams original;
  BOOST_CHECK(original.getDetection() == DETECT_CONTENT);
  original.setDetection(DETECT_TOP_EDGE);

  QDomDocument doc;
  const QDomElement el = original.toXml(doc, "deskew-params");
  doc.appendChild(el);

  const DefaultParams::DeskewParams restored(doc.documentElement());
  BOOST_CHECK(restored.getDetection() == DETECT_TOP_EDGE);
}

BOOST_AUTO_TEST_CASE(params_remeasure_when_any_angle_is_automatic) {
  QPolygonF outline;
  outline << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100);
  const Dependencies measuredOn(outline, OrthogonalRotation());
  const Dependencies otherImage;

  // An automatic oblique angle is measured again even when the deskew angle was set by hand.
  BOOST_CHECK(Params(1.0, 2.0, measuredOn, MODE_MANUAL, MODE_AUTO).needsRemeasure(otherImage));
  BOOST_CHECK(Params(1.0, 2.0, measuredOn, MODE_AUTO, MODE_MANUAL).needsRemeasure(otherImage));
  BOOST_CHECK(!Params(1.0, 2.0, measuredOn, MODE_MANUAL, MODE_MANUAL).needsRemeasure(otherImage));
  BOOST_CHECK(!Params(1.0, 2.0, measuredOn, MODE_AUTO, MODE_AUTO).needsRemeasure(measuredOn));
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(ImageTransformationObliqueTestSuite)

BOOST_AUTO_TEST_CASE(post_oblique_applied_and_stored) {
  const QRectF rect(0, 0, 100, 100);
  const Dpi dpi(300, 300);
  ImageTransformation xform(rect, dpi);

  QPolygonF preCrop;
  preCrop << rect.topLeft() << rect.topRight() << rect.bottomRight() << rect.bottomLeft();
  xform.setPreCropArea(preCrop);

  xform.setPostOblique(0.0);
  const QTransform transformZeroOblique = xform.transform();
  const double obliqueDeg = 2.0;
  xform.setPostOblique(obliqueDeg);

  BOOST_CHECK_CLOSE(xform.postOblique(), obliqueDeg, 1e-6);
  BOOST_CHECK(transformZeroOblique != xform.transform());
}

// The masks of the deskew step are drawn on the untransformed image.
BOOST_AUTO_TEST_CASE(orig_pre_crop_area_is_in_image_coordinates) {
  const QRectF imageRect(0, 0, 200, 100);
  // The right page of a split spread.
  ImageTransformation split(imageRect, Dpi(300, 300));
  split.setPreCropArea(QPolygonF(QRectF(100, 0, 100, 100)));
  const QRectF splitArea = split.origPreCropArea().boundingRect();
  BOOST_CHECK_CLOSE(splitArea.left(), 100.0, 1e-6);
  BOOST_CHECK_CLOSE(splitArea.width(), 100.0, 1e-6);

  // A page turned by 90 degrees.
  ImageTransformation rotated(imageRect, Dpi(300, 300));
  OrthogonalRotation rotation;
  rotation.nextClockwiseDirection();
  rotated.setPreRotation(rotation);
  const QRectF rotatedArea = rotated.origPreCropArea().boundingRect();
  BOOST_CHECK_CLOSE(rotatedArea.width(), 200.0, 1e-6);
  BOOST_CHECK_CLOSE(rotatedArea.height(), 100.0, 1e-6);
}

// A shear keeps the heights, so doubling the DPI has to double both axes and keep
// the aspect ratio. Measured with the shear, the height grew by 1/cos(oblique).
BOOST_AUTO_TEST_CASE(post_scale_keeps_aspect_ratio_with_oblique) {
  for (const double rotationDeg : {0.0, 3.0}) {
    const QRectF rect(0, 0, 1000, 1000);
    ImageTransformation xform(rect, Dpi(300, 300));
    xform.setPreCropArea(QPolygonF(rect));
    xform.setPostRotation(rotationDeg);
    xform.setPostOblique(5.0);
    const QTransform unscaled = xform.transform();

    xform.postScaleToDpi(Dpi(600, 600));
    const QTransform scaled = xform.transform();
    BOOST_CHECK_CLOSE(scaled.m11(), 2.0 * unscaled.m11(), 1e-6);
    BOOST_CHECK_CLOSE(scaled.m12(), 2.0 * unscaled.m12(), 1e-6);
    BOOST_CHECK_CLOSE(scaled.m21(), 2.0 * unscaled.m21(), 1e-6);
    BOOST_CHECK_CLOSE(scaled.m22(), 2.0 * unscaled.m22(), 1e-6);
  }
}

// The deskew handles are drawn with these; the shear must not change them.
BOOST_AUTO_TEST_CASE(post_rotation_sin_cos_ignore_oblique) {
  const QRectF rect(0, 0, 1000, 1000);
  ImageTransformation xform(rect, Dpi(300, 300));
  xform.setPreCropArea(QPolygonF(rect));
  xform.setPostRotation(5.0);
  xform.setPostOblique(5.0);
  BOOST_CHECK_CLOSE(xform.postRotationCos(), std::cos(qDegreesToRadians(5.0)), 1e-6);
  BOOST_CHECK_CLOSE(xform.postRotationSin(), std::sin(qDegreesToRadians(5.0)), 1e-6);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
