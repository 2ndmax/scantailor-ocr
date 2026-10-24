// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <filters/output/ColorParams.h>
#include <filters/output/RenderParams.h>
#include <filters/output/SplittingOptions.h>

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
