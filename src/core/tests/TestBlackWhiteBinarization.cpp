// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <GrayImage.h>

#include <QImage>
#include <QSize>
#include <array>
#include <boost/test/unit_test.hpp>
#include <cstdint>

#include "filters/output/BlackWhiteBinarization.h"
#include "filters/output/BlackWhiteOptions.h"

using namespace imageproc;

namespace output {
namespace tests {

BOOST_AUTO_TEST_SUITE(BlackWhiteBinarizationTestSuite)

namespace {
// Lines of dark blocks on paper that gets darker towards the right.
QImage makeTextPage() {
  GrayImage image(QSize(131, 97));
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      const bool inTextLine = (y % 16) >= 4 && (y % 16) < 12;
      const bool inLetter = (x % 9) < 6 && ((x / 9 + y / 16) % 5) != 0;
      line[x] = static_cast<uint8_t>((inTextLine && inLetter) ? 30 : 230 - x / 2);
    }
  }
  return image.toQImage();
}

// The whole range of gray values, the same on every machine.
QImage makeNoisePage() {
  GrayImage image(QSize(64, 64));
  for (int y = 0; y < image.height(); ++y) {
    uint8_t* line = image.data() + y * image.stride();
    for (int x = 0; x < image.width(); ++x) {
      line[x] = static_cast<uint8_t>(((x * 7919 + y * 104729) ^ (x * y * 31)) & 255);
    }
  }
  return image.toQImage();
}

// See TestBinarize.cpp for why the expected checksums only hold without fused multiply-add.
#if (defined(_M_X64) || defined(__x86_64__)) && !defined(__FMA__)
constexpr bool kChecksumsApply = true;
#else
constexpr bool kChecksumsApply = false;
#endif

// FNV-1a over the size and the pixels of the images.
class Checksum {
 public:
  void add(const BinaryImage& image) {
    addByte(image.width() & 255);
    addByte(image.height() & 255);
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        addByte(image.getPixel(x, y) == BLACK ? 1 : 0);
      }
    }
  }

  uint32_t value() const { return m_value; }

 private:
  void addByte(const uint32_t byte) { m_value = (m_value ^ byte) * 16777619u; }

  uint32_t m_value = 2166136261u;
};

struct Bounds {
  int lower;
  int upper;
};

// The default bounds, which most methods leave out, and narrower ones.
const Bounds kBounds[] = {{1, 254}, {110, 150}};

struct Expected {
  BinarizationMethod method;
  std::array<uint32_t, 2> perBounds;
};

// clang-format off
const Expected kExpected[] = {
    {T_OTSU,       {0x78E6121F, 0x78E6121F}},
    {T_SAUVOLA,    {0xF6FFD737, 0x958F595D}},
    {T_WOLF,       {0x35A8DCFE, 0x35A8DCFE}},
    {T_FOX,        {0xFADE9E9A, 0xFADE9E9A}},
    {T_WINDOW,     {0xBB990AE0, 0x29841C04}},
    {T_BRADLEY,    {0xE8265BCE, 0x820C697E}},
    {T_GRAD,       {0x3FFAB14A, 0x3FFAB14A}},
    {T_EDGEPLUS,   {0x6F1F93BD, 0x8268CC5D}},
    {T_BLURDIV,    {0x4D337BC2, 0x4D337BC2}},
    {T_EDGEDIV,    {0xA009F31C, 0xA009F31C}},
    {T_GATOS,      {0xA6D42B77, 0x820C697E}},
    {T_NICK,       {0x3F68A2A7, 0x820C697E}},
    {T_SINGH,      {0xCFFCA85C, 0x820C697E}},
    {T_ROBUST,     {0x9147A855, 0x5DF85575}},
    {T_MULTISCALE, {0xD0BC37A7, 0xD0BC37A7}},
};
// clang-format on
}  // namespace

// Holds what each method makes of the options, bit by bit. The two strengths differ from their
// defaults and from each other, so that a method reading the wrong one shows up.
BOOST_AUTO_TEST_CASE(test_every_method_reads_its_own_settings) {
  const QImage pages[] = {makeTextPage(), makeNoisePage()};

  for (const Expected& expected : kExpected) {
    for (size_t boundsIndex = 0; boundsIndex < expected.perBounds.size(); ++boundsIndex) {
      BOOST_TEST_CONTEXT("method " << static_cast<int>(expected.method) << ", bounds " << boundsIndex) {
        BlackWhiteOptions options;
        options.setBinarizationMethod(expected.method);
        options.setWindowSize(31);
        options.setSauvolaCoef(0.41);
        options.setWolfCoef(0.17);
        options.setThresholdAdjustment(7);
        options.setWolfLowerBound(kBounds[boundsIndex].lower);
        options.setWolfUpperBound(kBounds[boundsIndex].upper);

        Checksum checksum;
        for (const QImage& page : pages) {
          const BinaryImage result = binarizeBlackWhite(page, options);
          BOOST_CHECK_EQUAL(result.size().width(), page.width());
          checksum.add(result);
        }
        if (kChecksumsApply) {
          BOOST_CHECK_EQUAL(checksum.value(), expected.perBounds[boundsIndex]);
        }
      }
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace output
