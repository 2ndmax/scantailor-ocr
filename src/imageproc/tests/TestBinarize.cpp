// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Binarize.h>
#include <BinaryImage.h>
#include <ExperimentalBinarization.h>
#include <GrayImage.h>

#include <QImage>
#include <QSize>
#include <array>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace imageproc {
namespace tests {

BOOST_AUTO_TEST_SUITE(BinarizeTestSuite)

namespace {
template <typename PixelFunction>
GrayImage drawPage(const QSize size, PixelFunction pixel) {
  GrayImage image(size);
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      line[x] = static_cast<uint8_t>(pixel(x, y));
    }
  }
  return image;
}

// Lines of dark blocks, like words on paper.
int textPixel(const int x, const int y) {
  const bool inTextLine = (y % 16) >= 4 && (y % 16) < 12;
  const bool inLetter = (x % 9) < 6;
  const bool inWordGap = ((x / 9 + y / 16) % 5) == 0;
  return (inTextLine && inLetter && !inWordGap) ? 30 : 225;
}

// Paper getting darker to the right and downwards, with strokes that follow the shading.
int shadedPixel(const int x, const int y) {
  const int paper = 240 - x - y / 2;
  return ((x % 17) < 2) ? paper / 3 : paper;
}

// Light strokes on a dark ground.
int darkGroundPixel(const int x, const int y) {
  return ((x % 12) < 3 && (y % 20) < 14) ? 235 : 25;
}

// The whole range of gray values, the same on every machine.
int noisePixel(const int x, const int y) {
  return ((x * 7919 + y * 104729) ^ (x * y * 31)) & 255;
}

struct Page {
  std::string name;
  GrayImage image;
};

std::vector<Page> makePages() {
  return {
      {"text", drawPage(QSize(131, 97), textPixel)},
      {"shaded", drawPage(QSize(120, 90), shadedPixel)},
      {"dark ground", drawPage(QSize(90, 120), darkGroundPixel)},
      {"noise", drawPage(QSize(64, 64), noisePixel)},
      {"blank", drawPage(QSize(50, 40), [](int, int) { return 128; })},
      {"black", drawPage(QSize(40, 50), [](int, int) { return 0; })},
      {"one row", drawPage(QSize(73, 1), noisePixel)},
      {"one column", drawPage(QSize(1, 73), noisePixel)},
      {"one pixel", drawPage(QSize(1, 1), [](int, int) { return 77; })},
  };
}

constexpr size_t kPageCount = 9;

struct Settings {
  double k;
  int delta;
  unsigned char lowerBound;
  unsigned char upperBound;
};

using Binarizer = std::function<BinaryImage(const GrayImage&, QSize, const Settings&)>;

// Results for the same pages, expected per page.
struct MethodResults {
  std::string name;
  Binarizer binarize;
  std::array<uint32_t, kPageCount> expected;
};

// Even, odd, different in width and height, the smallest ones, and larger than every page. The
// experimental methods only use the width.
const QSize kWindows[] = {QSize(20, 20), QSize(21, 21), QSize(30, 11), QSize(1, 1), QSize(2, 3), QSize(500, 500)};

const Settings kSettings[] = {{0.34, 0, 0, 255}, {0.2, 10, 40, 230}, {0.6, -20, 0, 255}, {1.0, 5, 0, 255}};

// The expected checksums hold on x86-64 with plain SSE arithmetic (MSVC and GCC give the same).
// With fused multiply-add, as on ARM64 or with -march=native, a compiler may round differently,
// which can move a threshold across the value of a pixel.
#if (defined(_M_X64) || defined(__x86_64__)) && !defined(__FMA__)
constexpr bool kChecksumsApply = true;
#else
constexpr bool kChecksumsApply = false;
#endif

// FNV-1a over the size and the pixels of the image.
class Checksum {
 public:
  void add(const BinaryImage& image) {
    addValue(image.width());
    addValue(image.height());
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        addByte(image.getPixel(x, y) == BLACK ? 1 : 0);
      }
    }
  }

  uint32_t value() const { return m_value; }

 private:
  void addValue(const int value) {
    addByte(value & 255);
    addByte((value >> 8) & 255);
  }

  void addByte(const uint32_t byte) { m_value = (m_value ^ byte) * 16777619u; }

  uint32_t m_value = 2166136261u;
};

std::vector<MethodResults> makeMethods() {
  // clang-format off
  return {
      {"Sauvola", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeSauvola(g.toQImage(), w, s.k, s.delta); },
       {0x0AD8F25D, 0xDCAB43A9, 0x779500A0, 0xF5234BD0, 0x0DAA17C5, 0x851F67C5, 0xBDEC8147, 0x9BB4624D, 0xDE1F4BA5}},
      {"Wolf", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeWolf(g.toQImage(), w, s.lowerBound, s.upperBound, s.k, s.delta); },
       {0xF57C3C57, 0xD5111435, 0xDA0D34FF, 0xB6939428, 0x0DAA17C5, 0x62F8A225, 0x43CF4D8B, 0xCC6D09C8, 0xDE1F4BA5}},
      {"Fox", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeFox(g.toQImage(), w, s.lowerBound, s.upperBound, s.k, s.delta); },
       {0x5CBC3C8F, 0x97241169, 0xDA0D34FF, 0xFA224418, 0x0DAA17C5, 0x62F8A225, 0xE88F2DD1, 0x7A145AF6, 0xDE1F4BA5}},
      {"Window", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeWindow(g.toQImage(), w, s.lowerBound, s.upperBound, s.k, s.delta); },
       {0x2844D96F, 0xB0FEEB61, 0x3702E561, 0x20DB2DA6, 0x0DAA17C5, 0x62F8A225, 0x006528EF, 0x47D3CF0A, 0xDE1F4BA5}},
      {"Bradley", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeBradley(g.toQImage(), w, s.k, s.delta); },
       {0x81B0068B, 0xB16D8B68, 0xB4B5AE3C, 0x76035F0C, 0x0DAA17C5, 0xB38DF685, 0x90DCF163, 0xBD1B79A1, 0xDE1F4BA5}},
      {"Grad", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeGrad(g.toQImage(), w, s.lowerBound, s.upperBound, s.k, s.delta); },
       {0x4341A259, 0x6B82AC35, 0x0557EAD4, 0x6FF7BD9B, 0x2EA53685, 0x64F276C5, 0xB45B9010, 0xFDE05251, 0xA40E39B5}},
      {"EdgePlus", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeEdgeDiv(g.toQImage(), w, s.k, 0.0, s.delta); },
       {0x635B9E45, 0x658A888C, 0x68D28D45, 0xA5D1DC9E, 0x0DAA17C5, 0xB38DF685, 0x95D3D41D, 0x124D9DD2, 0xDE1F4BA5}},
      {"BlurDiv", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeEdgeDiv(g.toQImage(), w, 0.0, s.k, s.delta); },
       {0x635B9E45, 0x8F53DA2B, 0x68D28D45, 0xDF4C6A9C, 0x0DAA17C5, 0xB38DF685, 0x99A78B4C, 0x21EE58C0, 0xDE1F4BA5}},
      {"EdgeDiv", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeEdgeDiv(g.toQImage(), w, s.k, 1.0 - s.k, s.delta); },
       {0x635B9E45, 0xE3F5D3BA, 0x68D28D45, 0x69CD3332, 0x0DAA17C5, 0xB38DF685, 0xC4FE4438, 0x071324BF, 0xDE1F4BA5}},
      {"Gatos", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeGatos(g, w.width(), s.k, s.delta); },
       {0x3BB8C321, 0xC0C076AD, 0xF1A65012, 0x3E0B36CF, 0x0DAA17C5, 0x851F67C5, 0xD7A77253, 0xB6EC4987, 0xDE1F4BA5}},
      {"Nick", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeNick(g, w.width(), s.k, s.delta); },
       {0x14A8558C, 0xF6C4817D, 0xD6A91E00, 0x1AAC8F30, 0x0DAA17C5, 0x851F67C5, 0x4C2B8265, 0xF9F4A564, 0xDE1F4BA5}},
      {"Singh", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeSingh(g, w.width(), s.k, s.delta); },
       {0x1464FE05, 0x32761431, 0xA60CBDE9, 0x28B564D7, 0x0DAA17C5, 0x851F67C5, 0x77B8D8D7, 0x31F34FE1, 0xDE1F4BA5}},
      {"Robust", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeRobust(g, w.width(), s.k, s.delta); },
       {0x4C563BB1, 0x641B9A5D, 0x45F3A70D, 0x14D01403, 0x2EA53685, 0xB38DF685, 0xEE6E6353, 0xBE4D2C90, 0x1E4AD515}},
      {"MultiScale", [](const GrayImage& g, QSize w, const Settings& s) {
         return binarizeMultiScale(g, w.width(), s.k, s.delta); },
       {0xCF558BCD, 0x820ADDBD, 0x2F4E8EC5, 0x24D3FFDD, 0x2EA53685, 0xB38DF685, 0xFC76A6B2, 0x62147821, 0x1E4AD515}},
  };
  // clang-format on
}
}  // namespace

// Holds the results of all window based methods bit by bit, so that a change of the shared window
// code or of a formula shows up. A checksum stands for all windows and settings of one method and
// one page.
BOOST_AUTO_TEST_CASE(test_results_of_window_methods_stay_the_same) {
  const std::vector<Page> pages = makePages();
  BOOST_REQUIRE_EQUAL(pages.size(), kPageCount);

  for (const MethodResults& method : makeMethods()) {
    for (size_t pageIndex = 0; pageIndex < kPageCount; ++pageIndex) {
      BOOST_TEST_CONTEXT(method.name << " on page \"" << pages[pageIndex].name << "\"") {
        Checksum checksum;
        for (const QSize window : kWindows) {
          for (const Settings& settings : kSettings) {
            checksum.add(method.binarize(pages[pageIndex].image, window, settings));
          }
        }
        if (kChecksumsApply) {
          BOOST_CHECK_EQUAL(checksum.value(), method.expected[pageIndex]);
        }
      }
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
