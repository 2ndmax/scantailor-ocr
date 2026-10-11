// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <ExperimentalBinarization.h>
#include <GrayImage.h>

#include <boost/test/unit_test.hpp>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(ExperimentalBinarizationTestSuite)

namespace {
// Paper getting darker from 230 on the left to 170 on the right, as near a spine, with dark
// vertical strokes (ink of 40) every 20 pixels.
GrayImage makePage() {
  GrayImage image(QSize(400, 200));
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      const int paper = 230 - (60 * x) / image.width();
      const bool stroke = (y >= 50) && (y < 150) && ((x % 20) >= 8) && ((x % 20) < 12);
      line[x] = static_cast<uint8_t>(stroke ? 40 : paper);
    }
  }
  return image;
}

bool isStroke(const int x, const int y) {
  return (y >= 50) && (y < 150) && ((x % 20) >= 8) && ((x % 20) < 12);
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_strokes_black_paper_white) {
  const GrayImage page = makePage();
  const std::vector<std::pair<std::string, std::function<BinaryImage(const GrayImage&)>>> methods = {
      {"Gatos", [](const GrayImage& g) { return binarizeGatos(g); }},
      {"Nick", [](const GrayImage& g) { return binarizeNick(g); }},
      {"Singh", [](const GrayImage& g) { return binarizeSingh(g); }},
      {"Robust", [](const GrayImage& g) { return binarizeRobust(g); }},
      {"MultiScale", [](const GrayImage& g) { return binarizeMultiScale(g); }},
  };
  for (const auto& [name, binarize] : methods) {
    BOOST_TEST_CONTEXT(name) {
      const BinaryImage result = binarize(page);
      BOOST_REQUIRE_EQUAL(result.width(), page.width());
      BOOST_REQUIRE_EQUAL(result.height(), page.height());
      // Away from the image border, where the windows are cut off.
      int wrongStrokes = 0;
      int wrongPaper = 0;
      int strokes = 0;
      int paper = 0;
      for (int y = 20; y < 180; ++y) {
        for (int x = 20; x < 380; ++x) {
          const bool black = result.getPixel(x, y) == BLACK;
          if (isStroke(x, y)) {
            ++strokes;
            wrongStrokes += black ? 0 : 1;
          } else {
            ++paper;
            wrongPaper += black ? 1 : 0;
          }
        }
      }
      BOOST_CHECK_LT(wrongStrokes, strokes / 20);
      BOOST_CHECK_LT(wrongPaper, paper / 20);
      // The same input gives the same result.
      BOOST_CHECK(binarize(page) == result);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
