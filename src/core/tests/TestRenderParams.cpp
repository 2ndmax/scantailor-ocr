// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/output/BlackWhiteOptions.h>
#include <filters/output/ColorCommonOptions.h>
#include <filters/output/ColorParams.h>
#include <filters/output/Params.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/output/RenderParams.h>
#include <filters/output/SplittingOptions.h>

#include <QDomDocument>
#include <boost/test/unit_test.hpp>
#include <utility>
#include <vector>

namespace Tests {
using namespace output;

namespace {
ColorParams makeColorParams(const ColorMode mode, const bool normalizeBw, const bool normalizeColor) {
  ColorParams params;
  params.setColorMode(mode);
  BlackWhiteOptions blackWhiteOptions(params.blackWhiteOptions());
  blackWhiteOptions.setNormalizeIllumination(normalizeBw);
  params.setBlackWhiteOptions(blackWhiteOptions);
  ColorCommonOptions colorCommonOptions(params.colorCommonOptions());
  colorCommonOptions.setNormalizeIllumination(normalizeColor);
  params.setColorCommonOptions(colorCommonOptions);
  return params;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RenderParamsTestSuite)

BOOST_AUTO_TEST_CASE(test_mixed_equalizes_pictures_only_with_text) {
  // In mixed mode the pictures are only equalized together with the text, whatever the stored
  // color setting is.
  const RenderParams off(makeColorParams(MIXED, false, true), SplittingOptions());
  BOOST_CHECK(!off.normalizeIllumination());
  BOOST_CHECK(!off.normalizeIlluminationColor());

  const RenderParams on(makeColorParams(MIXED, true, true), SplittingOptions());
  BOOST_CHECK(on.normalizeIllumination());
  BOOST_CHECK(on.normalizeIlluminationColor());

  const RenderParams textOnly(makeColorParams(MIXED, true, false), SplittingOptions());
  BOOST_CHECK(textOnly.normalizeIllumination());
  BOOST_CHECK(!textOnly.normalizeIlluminationColor());
}

BOOST_AUTO_TEST_CASE(test_mixed_color_foreground_follows_the_same_rule) {
  // There is no binarization with a color foreground, but the color setting still counts only
  // together with the black and white one.
  SplittingOptions splittingOptions;
  splittingOptions.setSplitOutput(true);
  splittingOptions.setSplittingMode(COLOR_FOREGROUND);

  const RenderParams off(makeColorParams(MIXED, false, true), splittingOptions);
  BOOST_CHECK(!off.normalizeIllumination());
  BOOST_CHECK(!off.normalizeIlluminationColor());

  const RenderParams on(makeColorParams(MIXED, true, true), splittingOptions);
  BOOST_CHECK(on.normalizeIllumination());
  BOOST_CHECK(on.normalizeIlluminationColor());

  const RenderParams textOnly(makeColorParams(MIXED, true, false), splittingOptions);
  BOOST_CHECK(!textOnly.normalizeIllumination());
  BOOST_CHECK(!textOnly.normalizeIlluminationColor());
}

BOOST_AUTO_TEST_CASE(test_color_mode_uses_the_color_setting) {
  const RenderParams on(makeColorParams(COLOR_GRAYSCALE, false, true), SplittingOptions());
  BOOST_CHECK(on.normalizeIllumination());
  BOOST_CHECK(on.normalizeIlluminationColor());

  const RenderParams off(makeColorParams(COLOR_GRAYSCALE, true, false), SplittingOptions());
  BOOST_CHECK(!off.normalizeIllumination());
  BOOST_CHECK(!off.normalizeIlluminationColor());
}

// A project file without these attributes (edited by hand or from an older version) gets the
// defaults, not 0: an upper bound of 0 would leave the page almost blank.
BOOST_AUTO_TEST_CASE(missing_output_attributes_get_defaults) {
  QDomDocument doc;
  const QDomElement empty = doc.createElement("options");
  const BlackWhiteOptions defaults;
  const BlackWhiteOptions bw(empty);
  BOOST_CHECK_EQUAL(bw.getWindowSize(), defaults.getWindowSize());
  BOOST_CHECK_EQUAL(bw.getSauvolaCoef(), defaults.getSauvolaCoef());
  BOOST_CHECK_EQUAL(bw.getWolfLowerBound(), defaults.getWolfLowerBound());
  BOOST_CHECK_EQUAL(bw.getWolfUpperBound(), defaults.getWolfUpperBound());
  BOOST_CHECK_EQUAL(bw.getWolfCoef(), defaults.getWolfCoef());
  BOOST_CHECK_EQUAL(BlackWhiteOptions::ColorSegmenterOptions(empty).getNoiseReduction(),
                    BlackWhiteOptions::ColorSegmenterOptions().getNoiseReduction());
  BOOST_CHECK_EQUAL(PictureShapeOptions(empty).getSensitivity(), PictureShapeOptions().getSensitivity());
  BOOST_CHECK_EQUAL(ColorCommonOptions::PosterizationOptions(empty).getLevel(),
                    ColorCommonOptions::PosterizationOptions().getLevel());
  BOOST_CHECK_EQUAL(Params(empty).despeckleLevel(), 1.0);
}

// ScanTailor stored the despeckle level as a word; the number is the same level on the slider.
BOOST_AUTO_TEST_CASE(despeckle_level_as_word) {
  const std::vector<std::pair<QString, double>> cases
      = {{"off", 0.0}, {"cautious", 1.0}, {"normal", 2.0}, {"aggressive", 3.0}, {"2.5", 2.5}};
  for (const auto& [value, expected] : cases) {
    QDomDocument doc;
    QDomElement el = doc.createElement("params");
    el.setAttribute("despeckleLevel", value);
    BOOST_CHECK_MESSAGE(Params(el).despeckleLevel() == expected, value.toStdString());
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
