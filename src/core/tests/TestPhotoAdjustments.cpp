// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <Grayscale.h>
#include <filters/output/ColorParams.h>
#include <filters/output/PhotoAdjustments.h>

#include <QDomDocument>
#include <QImage>
#include <boost/test/unit_test.hpp>

namespace Tests {
using namespace output;
using namespace imageproc;

namespace {
// A 256 x 1 grayscale image with all values.
QImage grayRamp() {
  QImage image(256, 1, QImage::Format_Indexed8);
  image.setColorTable(createGrayscalePalette());
  for (int x = 0; x < 256; ++x) {
    image.scanLine(0)[x] = static_cast<uint8_t>(x);
  }
  return image;
}

QImage colorImage(const QRgb color, const int width = 2) {
  QImage image(width, 1, QImage::Format_RGB32);
  image.fill(color);
  return image;
}

QImage invertedCopy(const QImage& image) {
  QImage copy(image);
  copy.invertPixels();
  return copy;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PhotoAdjustmentsTestSuite)

BOOST_AUTO_TEST_CASE(default_is_neutral_and_changes_nothing) {
  const PhotoAdjustments adjustments;
  BOOST_CHECK(adjustments.isDefault());
  BOOST_CHECK(adjustments.isNeutral());

  uint8_t table[256];
  adjustments.buildToneTable(table);
  for (int i = 0; i < 256; ++i) {
    BOOST_REQUIRE_EQUAL(table[i], i);
  }

  QImage image = grayRamp();
  const QImage before = image;
  adjustments.applyInPlace(image);
  BOOST_CHECK(image == before);
}

BOOST_AUTO_TEST_CASE(keep_white_alone_is_stored_but_neutral) {
  PhotoAdjustments adjustments;
  adjustments.setKeepWhite(true);
  BOOST_CHECK(!adjustments.isDefault());
  BOOST_CHECK(adjustments.isNeutral());
}

BOOST_AUTO_TEST_CASE(values_are_clamped) {
  PhotoAdjustments adjustments;
  adjustments.setContrast(500);
  adjustments.setShadows(-500);
  adjustments.setExposure(7.0);
  BOOST_CHECK_EQUAL(adjustments.contrast(), 100);
  BOOST_CHECK_EQUAL(adjustments.shadows(), -100);
  BOOST_CHECK_EQUAL(adjustments.exposure(), 2.0);
}

BOOST_AUTO_TEST_CASE(xml_round_trip) {
  PhotoAdjustments adjustments;
  adjustments.setTemperature(20);
  adjustments.setTint(-10);
  adjustments.setExposure(0.35);
  adjustments.setContrast(15);
  adjustments.setHighlights(-30);
  adjustments.setShadows(25);
  adjustments.setWhites(5);
  adjustments.setBlacks(-5);
  adjustments.setKeepWhite(true);

  QDomDocument doc;
  const PhotoAdjustments restored(adjustments.toXml(doc, "photo-adjustments"));
  BOOST_CHECK(restored == adjustments);
}

BOOST_AUTO_TEST_CASE(color_params_store_adjustments_only_when_used) {
  QDomDocument doc;
  ColorParams params;
  params.setColorMode(COLOR_GRAYSCALE);
  BOOST_CHECK(params.toXml(doc, "color-params").namedItem("photo-adjustments").isNull());

  PhotoAdjustments adjustments;
  adjustments.setExposure(-0.5);
  params.setPhotoAdjustments(adjustments);
  const QDomElement el = params.toXml(doc, "color-params");
  BOOST_CHECK(!el.namedItem("photo-adjustments").isNull());
  BOOST_CHECK(ColorParams(el).photoAdjustments() == adjustments);
}

BOOST_AUTO_TEST_CASE(exposure_brightens_and_darkens) {
  PhotoAdjustments brighter;
  brighter.setExposure(1.0);
  QImage image = grayRamp();
  brighter.applyInPlace(image);
  BOOST_CHECK_EQUAL(image.scanLine(0)[0], 0);
  BOOST_CHECK_EQUAL(image.scanLine(0)[64], 128);
  BOOST_CHECK_EQUAL(image.scanLine(0)[200], 255);

  PhotoAdjustments darker;
  darker.setExposure(-1.0);
  image = grayRamp();
  darker.applyInPlace(image);
  BOOST_CHECK_EQUAL(image.scanLine(0)[255], 128);
}

BOOST_AUTO_TEST_CASE(keep_white_protects_pure_white_only) {
  PhotoAdjustments adjustments;
  adjustments.setExposure(-1.0);
  adjustments.setKeepWhite(true);

  QImage gray = grayRamp();
  adjustments.applyInPlace(gray);
  BOOST_CHECK_EQUAL(gray.scanLine(0)[255], 255);
  BOOST_CHECK_EQUAL(gray.scanLine(0)[254], 127);

  QImage white = colorImage(qRgb(255, 255, 255));
  adjustments.applyInPlace(white);
  BOOST_CHECK_EQUAL(white.pixel(0, 0) & 0xFFFFFFu, 0xFFFFFFu);

  QImage almostWhite = colorImage(qRgb(255, 255, 254));
  adjustments.applyInPlace(almostWhite);
  BOOST_CHECK_EQUAL(qRed(almostWhite.pixel(0, 0)), 128);
}

BOOST_AUTO_TEST_CASE(only_white_mask_pixels_change) {
  PhotoAdjustments adjustments;
  adjustments.setExposure(1.0);

  QImage image(2, 1, QImage::Format_Indexed8);
  image.setColorTable(createGrayscalePalette());
  image.scanLine(0)[0] = 64;
  image.scanLine(0)[1] = 64;

  BinaryImage mask(2, 1, WHITE);
  mask.setPixel(1, 0, BLACK);
  adjustments.applyInPlace(image, &mask);
  BOOST_CHECK_EQUAL(image.scanLine(0)[0], 128);
  BOOST_CHECK_EQUAL(image.scanLine(0)[1], 64);
}

BOOST_AUTO_TEST_CASE(inverted_images_are_adjusted_as_shown) {
  PhotoAdjustments adjustments;
  adjustments.setExposure(0.5);
  adjustments.setContrast(20);
  adjustments.setTemperature(30);
  adjustments.setKeepWhite(true);

  QImage normal = colorImage(qRgb(200, 120, 40));
  normal.setPixel(1, 0, qRgb(255, 255, 255));
  QImage inverted = invertedCopy(normal);

  adjustments.applyInPlace(normal);
  adjustments.applyInPlace(inverted, nullptr, true);
  BOOST_CHECK(invertedCopy(inverted) == normal);
}

BOOST_AUTO_TEST_CASE(temperature_warms_and_tint_changes_green) {
  PhotoAdjustments warmer;
  warmer.setTemperature(50);
  QImage image = colorImage(qRgb(128, 128, 128));
  warmer.applyInPlace(image);
  BOOST_CHECK_GT(qRed(image.pixel(0, 0)), 128);
  BOOST_CHECK_EQUAL(qGreen(image.pixel(0, 0)), 128);
  BOOST_CHECK_LT(qBlue(image.pixel(0, 0)), 128);

  PhotoAdjustments magenta;
  magenta.setTint(50);
  image = colorImage(qRgb(128, 128, 128));
  magenta.applyInPlace(image);
  BOOST_CHECK_LT(qGreen(image.pixel(0, 0)), 128);
}

BOOST_AUTO_TEST_CASE(highlights_and_shadows_brighten_when_positive) {
  PhotoAdjustments highlights;
  highlights.setHighlights(50);
  uint8_t table[256];
  highlights.buildToneTable(table);
  BOOST_CHECK_GT(table[200], 200);
  BOOST_CHECK_EQUAL(table[20], 20);

  PhotoAdjustments shadows;
  shadows.setShadows(50);
  shadows.buildToneTable(table);
  BOOST_CHECK_GT(table[40], 40);
  BOOST_CHECK_EQUAL(table[250], 250);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
