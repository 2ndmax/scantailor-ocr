// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <GrayImage.h>

#include <QDomDocument>
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
  const char* name;
  std::array<uint32_t, 2> perBounds;
};

// clang-format off
const Expected kExpected[] = {
    {T_OTSU,       "otsu",       {0x78E6121F, 0x78E6121F}},
    {T_SAUVOLA,    "sauvola",    {0xF6FFD737, 0x958F595D}},
    {T_WOLF,       "wolf",       {0x35A8DCFE, 0x35A8DCFE}},
    {T_FOX,        "fox",        {0xFADE9E9A, 0xFADE9E9A}},
    {T_WINDOW,     "window",     {0xBB990AE0, 0x29841C04}},
    {T_BRADLEY,    "bradley",    {0xE8265BCE, 0x820C697E}},
    {T_GRAD,       "grad",       {0x3FFAB14A, 0x3FFAB14A}},
    {T_EDGEPLUS,   "edgeplus",   {0x6F1F93BD, 0x8268CC5D}},
    {T_BLURDIV,    "blurdiv",    {0x4D337BC2, 0x4D337BC2}},
    {T_EDGEDIV,    "edgediv",    {0xA009F31C, 0xA009F31C}},
    {T_GATOS,      "gatos",      {0xA6D42B77, 0x820C697E}},
    {T_NICK,       "nick",       {0x3F68A2A7, 0x820C697E}},
    {T_SINGH,      "singh",      {0xCFFCA85C, 0x820C697E}},
    {T_ROBUST,     "robust",     {0x9147A855, 0x5DF85575}},
    {T_MULTISCALE, "multiscale", {0xD0BC37A7, 0xD0BC37A7}},
};
// clang-format on
}  // namespace

// Holds what each method makes of the options, bit by bit.
BOOST_AUTO_TEST_CASE(test_every_method_reads_its_own_settings) {
  const QImage pages[] = {makeTextPage(), makeNoisePage()};

  for (const Expected& expected : kExpected) {
    for (size_t boundsIndex = 0; boundsIndex < expected.perBounds.size(); ++boundsIndex) {
      BOOST_TEST_CONTEXT("method " << static_cast<int>(expected.method) << ", bounds " << boundsIndex) {
        BlackWhiteOptions options;
        options.setBinarizationMethod(expected.method);
        const bool strengthOfWolf = (expected.method == T_WOLF) || (expected.method == T_FOX)
                                    || (expected.method == T_WINDOW) || (expected.method == T_GRAD);
        options.setValues(expected.method, {7, strengthOfWolf ? 0.17 : 0.41, 31});
        options.setLowerBound(kBounds[boundsIndex].lower);
        options.setUpperBound(kBounds[boundsIndex].upper);

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

// A project file of ScanTailor Advanced holds the values as attributes of one element. The page has
// to come out as its method makes it with these values, so the checksums are the ones above.
BOOST_AUTO_TEST_CASE(test_a_file_with_the_values_as_attributes_gives_the_same_result) {
  const QImage pages[] = {makeTextPage(), makeNoisePage()};

  for (const Expected& expected : kExpected) {
    for (size_t boundsIndex = 0; boundsIndex < expected.perBounds.size(); ++boundsIndex) {
      BOOST_TEST_CONTEXT("method " << expected.name << ", bounds " << boundsIndex) {
        QDomDocument doc;
        QDomElement el = doc.createElement(QStringLiteral("bw"));
        el.setAttribute(QStringLiteral("binarizationMethod"), QString::fromLatin1(expected.name));
        el.setAttribute(QStringLiteral("windowSize"), 31);
        el.setAttribute(QStringLiteral("sauvolaCoef"), QStringLiteral("0.41"));
        el.setAttribute(QStringLiteral("wolfCoef"), QStringLiteral("0.17"));
        el.setAttribute(QStringLiteral("thresholdAdj"), 7);
        el.setAttribute(QStringLiteral("wolfLowerBound"), kBounds[boundsIndex].lower);
        el.setAttribute(QStringLiteral("wolfUpperBound"), kBounds[boundsIndex].upper);
        const BlackWhiteOptions options(el);

        Checksum checksum;
        for (const QImage& page : pages) {
          checksum.add(binarizeBlackWhite(page, options));
        }
        if (kChecksumsApply) {
          BOOST_CHECK_EQUAL(checksum.value(), expected.perBounds[boundsIndex]);
        }
      }
    }
  }
}

namespace {
const BlackWhiteOptions::MethodValues kChangedValues{3, 0.27, 120};

int methodElementCount(const QDomElement& el) {
  int count = 0;
  for (QDomElement method = el.firstChildElement(QStringLiteral("method")); !method.isNull();
       method = method.nextSiblingElement(QStringLiteral("method"))) {
    ++count;
  }
  return count;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_each_method_keeps_its_values_when_another_is_chosen) {
  BlackWhiteOptions options;
  options.setBinarizationMethod(T_GATOS);
  options.setValues(T_GATOS, kChangedValues);

  options.setBinarizationMethod(T_WOLF);
  BOOST_CHECK(options.values() == BlackWhiteOptions::defaultValues(T_WOLF));

  options.setBinarizationMethod(T_GATOS);
  BOOST_CHECK(options.values() == kChangedValues);
}

BOOST_AUTO_TEST_CASE(test_the_values_of_all_methods_survive_the_file) {
  BlackWhiteOptions options;
  options.setBinarizationMethod(T_ROBUST);
  options.setValues(T_GATOS, kChangedValues);
  options.setValues(
      T_OTSU, {5, BlackWhiteOptions::defaultValues(T_OTSU).coef, BlackWhiteOptions::defaultValues(T_OTSU).windowSize});
  options.setLowerBound(11);
  options.setUpperBound(240);

  QDomDocument doc;
  const QDomElement el = options.toXml(doc, QStringLiteral("bw"));

  BOOST_CHECK(BlackWhiteOptions(el) == options);
  // The method of the page and those that differ from their defaults.
  BOOST_CHECK_EQUAL(methodElementCount(el), 3);
}

BOOST_AUTO_TEST_CASE(test_values_in_attributes_count_for_the_method_of_the_page_only) {
  QDomDocument doc;
  QDomElement el = doc.createElement(QStringLiteral("bw"));
  el.setAttribute(QStringLiteral("binarizationMethod"), QStringLiteral("sauvola"));
  el.setAttribute(QStringLiteral("windowSize"), 31);
  el.setAttribute(QStringLiteral("sauvolaCoef"), QStringLiteral("0.41"));
  el.setAttribute(QStringLiteral("wolfCoef"), QStringLiteral("0.17"));
  el.setAttribute(QStringLiteral("thresholdAdj"), 7);
  el.setAttribute(QStringLiteral("wolfLowerBound"), 20);
  el.setAttribute(QStringLiteral("wolfUpperBound"), 200);

  const BlackWhiteOptions options(el);

  BOOST_CHECK(options.values(T_SAUVOLA) == (BlackWhiteOptions::MethodValues{7, 0.41, 31}));
  BOOST_CHECK_EQUAL(options.getLowerBound(), 20);
  BOOST_CHECK_EQUAL(options.getUpperBound(), 200);
  BOOST_CHECK(options.values(T_WOLF) == BlackWhiteOptions::defaultValues(T_WOLF));
  BOOST_CHECK(options.values(T_OTSU) == BlackWhiteOptions::defaultValues(T_OTSU));
}

BOOST_AUTO_TEST_CASE(test_missing_or_invalid_values_give_the_default_of_the_method) {
  QDomDocument doc;
  QDomElement el = doc.createElement(QStringLiteral("bw"));
  el.setAttribute(QStringLiteral("binarizationMethod"), QStringLiteral("robust"));
  BOOST_CHECK(BlackWhiteOptions(el).values() == BlackWhiteOptions::defaultValues(T_ROBUST));

  QDomElement method = doc.createElement(QStringLiteral("method"));
  method.setAttribute(QStringLiteral("name"), QStringLiteral("robust"));
  el.appendChild(method);
  BOOST_CHECK(BlackWhiteOptions(el).values() == BlackWhiteOptions::defaultValues(T_ROBUST));

  method.setAttribute(QStringLiteral("window"), 2);
  method.setAttribute(QStringLiteral("coef"), QStringLiteral("nan"));
  BOOST_CHECK(BlackWhiteOptions(el).values() == BlackWhiteOptions::defaultValues(T_ROBUST));

  method.setAttribute(QStringLiteral("window"), BlackWhiteOptions::MIN_WINDOW_SIZE);
  BOOST_CHECK_EQUAL(BlackWhiteOptions(el).values().windowSize, BlackWhiteOptions::MIN_WINDOW_SIZE);
}

// Far outside the fields: such a delta overflowed "threshold + delta", such a strength did not fit a float.
BOOST_AUTO_TEST_CASE(test_delta_and_strength_far_outside_their_fields_give_the_default) {
  const BlackWhiteOptions::MethodValues defaults = BlackWhiteOptions::defaultValues(T_SAUVOLA);

  QDomDocument doc;
  QDomElement attributes = doc.createElement(QStringLiteral("bw"));
  attributes.setAttribute(QStringLiteral("binarizationMethod"), QStringLiteral("sauvola"));
  attributes.setAttribute(QStringLiteral("thresholdAdj"), 2147483647);
  attributes.setAttribute(QStringLiteral("sauvolaCoef"), QStringLiteral("1e300"));
  BOOST_CHECK(BlackWhiteOptions(attributes).values(T_SAUVOLA) == defaults);

  QDomElement el = doc.createElement(QStringLiteral("bw"));
  QDomElement method = doc.createElement(QStringLiteral("method"));
  method.setAttribute(QStringLiteral("name"), QStringLiteral("sauvola"));
  method.setAttribute(QStringLiteral("delta"), -2147483647);
  method.setAttribute(QStringLiteral("coef"), QStringLiteral("-1e300"));
  el.appendChild(method);
  BOOST_CHECK(BlackWhiteOptions(el).values(T_SAUVOLA) == defaults);

  method.setAttribute(QStringLiteral("delta"), 100);
  method.setAttribute(QStringLiteral("coef"), QStringLiteral("9.99"));
  BOOST_CHECK_EQUAL(BlackWhiteOptions(el).values(T_SAUVOLA).thresholdAdjustment, 100);
  BOOST_CHECK_EQUAL(BlackWhiteOptions(el).values(T_SAUVOLA).coef, 9.99);
}

BOOST_AUTO_TEST_CASE(test_an_unknown_method_name_changes_nothing) {
  QDomDocument doc;
  QDomElement el = doc.createElement(QStringLiteral("bw"));
  QDomElement method = doc.createElement(QStringLiteral("method"));
  method.setAttribute(QStringLiteral("name"), QStringLiteral("no such method"));
  method.setAttribute(QStringLiteral("delta"), 9);
  el.appendChild(method);

  BOOST_CHECK(BlackWhiteOptions(el).values(T_OTSU) == BlackWhiteOptions::defaultValues(T_OTSU));
}

BOOST_AUTO_TEST_CASE(test_color_segmenter_values_outside_their_fields_give_the_default) {
  using SegmenterOptions = BlackWhiteOptions::ColorSegmenterOptions;
  QDomDocument doc;
  for (const QString& noiseReduction : {QStringLiteral("-1"), QStringLiteral("1000"), QStringLiteral("2147483647")}) {
    QDomElement el = doc.createElement(QStringLiteral("segmenter"));
    el.setAttribute(QStringLiteral("noiseReduction"), noiseReduction);
    BOOST_CHECK_EQUAL(SegmenterOptions(el).getNoiseReduction(), SegmenterOptions().getNoiseReduction());
  }
  for (const QString& adjustment : {QStringLiteral("-100"), QStringLiteral("100"), QStringLiteral("2147483647")}) {
    QDomElement el = doc.createElement(QStringLiteral("segmenter"));
    el.setAttribute(QStringLiteral("redThresholdAdjustment"), adjustment);
    el.setAttribute(QStringLiteral("greenThresholdAdjustment"), adjustment);
    el.setAttribute(QStringLiteral("blueThresholdAdjustment"), adjustment);
    const SegmenterOptions options(el);
    BOOST_CHECK_EQUAL(options.getRedThresholdAdjustment(), 0);
    BOOST_CHECK_EQUAL(options.getGreenThresholdAdjustment(), 0);
    BOOST_CHECK_EQUAL(options.getBlueThresholdAdjustment(), 0);
  }
}

BOOST_AUTO_TEST_CASE(test_color_segmenter_values_at_the_ends_of_their_fields_are_kept) {
  QDomDocument doc;
  QDomElement el = doc.createElement(QStringLiteral("segmenter"));
  el.setAttribute(QStringLiteral("noiseReduction"), 999);
  el.setAttribute(QStringLiteral("redThresholdAdjustment"), -99);
  el.setAttribute(QStringLiteral("greenThresholdAdjustment"), 99);
  const BlackWhiteOptions::ColorSegmenterOptions options(el);
  BOOST_CHECK_EQUAL(options.getNoiseReduction(), 999);
  BOOST_CHECK_EQUAL(options.getRedThresholdAdjustment(), -99);
  BOOST_CHECK_EQUAL(options.getGreenThresholdAdjustment(), 99);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace output
