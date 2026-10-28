// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AutoRemovingFile.h>
#include <BinaryImage.h>
#include <DebugImages.h>
#include <Despeckle.h>
#include <Dpi.h>
#include <NullTaskStatus.h>

#include <QImage>
#include <QRect>
#include <QStringList>
#include <boost/test/unit_test.hpp>
#include <cstdint>

namespace Tests {
using namespace imageproc;

namespace {
// Only records the labels, to see which steps of the despeckling ran.
class DebugLabels : public DebugImages {
 public:
  void add(const QImage&, const QString& label, const std::function<QWidget*(const QImage&)>&) override {
    labels.push_back(label);
  }

  void add(const BinaryImage&, const QString& label, const std::function<QWidget*(const QImage&)>&) override {
    labels.push_back(label);
  }

  bool empty() const override { return true; }

  AutoRemovingFile retrieveNext(QString*, std::function<QWidget*(const QImage&)>*) override {
    return AutoRemovingFile();
  }

  QStringList labels;
};

// The step that gives small components a second chance (voronoiSpecial in Despeckle.cpp).
const QString kSecondChance = QStringLiteral("voronoi_special");

QString toText(const BinaryImage& image) {
  QString text;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      text += image.getPixel(x, y) == BLACK ? QLatin1Char('#') : QLatin1Char('.');
    }
    text += QLatin1Char('/');
  }
  return text;
}

// FNV-1a over the pixels, independent of the unused bits at the end of each line.
uint64_t pixelHash(const BinaryImage& image) {
  uint64_t hash = 14695981039346656037ULL;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      hash = (hash ^ uint64_t(image.getPixel(x, y))) * 1099511628211ULL;
    }
  }
  return hash;
}

// A page-like test image: blocks of various sizes (some large enough to be kept for sure),
// and specks of one or a few pixels around them. The same seed gives the same image.
BinaryImage speckledPage(int width, int height, uint32_t seed) {
  uint32_t state = seed;
  auto next = [&state](int range) {
    state = state * 1664525u + 1013904223u;
    return int((state >> 8) % uint32_t(range));
  };
  BinaryImage image(width, height, WHITE);
  const int area = width * height;
  for (int i = 0; i < area / 400; ++i) {
    const int w = 1 + next(next(4) == 0 ? 30 : 8);
    const int h = 1 + next(next(4) == 0 ? 30 : 8);
    image.fill(QRect(next(width), next(height), w, h) & image.rect(), BLACK);
  }
  for (int i = 0; i < area / 150; ++i) {
    image.setPixel(next(width), next(height), BLACK);
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DespeckleTestSuite)

// A lone speck on the left: the distance propagation runs to the right of it, where the distance
// vectors are negative. They were shifted left, which is undefined behaviour (reported by UBSan).
BOOST_AUTO_TEST_CASE(speck_on_the_left_is_removed) {
  BinaryImage image(3, 1, WHITE);
  image.setPixel(0, 0, BLACK);
  const BinaryImage result = Despeckle::despeckle(image, Dpi(300, 300), Despeckle::NORMAL, NullTaskStatus());
  BOOST_CHECK_EQUAL(toText(result).toStdString(), ".../");
}

// A 3x3 block with a single pixel to its right: the block is only near a much smaller
// component, which makes despeckling give it a second chance. That pass also propagates
// negative distance vectors to the right.
BOOST_AUTO_TEST_CASE(second_chance_propagates_to_the_right) {
  BinaryImage image(10, 3, WHITE);
  image.fill(QRect(0, 0, 3, 3), BLACK);
  image.setPixel(6, 1, BLACK);
  DebugLabels steps;
  const BinaryImage result = Despeckle::despeckle(image, Dpi(300, 300), Despeckle::NORMAL, NullTaskStatus(), &steps);
  BOOST_CHECK(steps.labels.contains(kSecondChance));
  // Nothing is big enough to be kept for sure, so all goes.
  BOOST_CHECK_EQUAL(toText(result).toStdString(), "........../........../........../");
}

// Characterization: the results the despeckling gave before its two distance propagations were
// merged into one. Each image runs through the second-chance pass with at least one level.
BOOST_AUTO_TEST_CASE(results_are_unchanged) {
  struct Case {
    int width;
    int height;
    uint32_t seed;
    uint64_t expected[5];  // CAUTIOUS, NORMAL, AGGRESSIVE, level 1.5, level 3.5
  };
  const Case cases[] = {
      {97,
       61,
       1,
       {17881253642145317734ULL, 10603582864122148232ULL, 4700053299609924385ULL, 16111607433422974454ULL,
        7457230811678064203ULL}},
      {256,
       193,
       2,
       {5497798018484983095ULL, 16937705649873432609ULL, 2249048268174054201ULL, 11166006420024884865ULL,
        7231912293980979645ULL}},
      {400,
       300,
       3,
       {15346444912387215636ULL, 17868396167586470820ULL, 10582712417093513975ULL, 6468878910905927857ULL,
        5388979416861117000ULL}},
  };
  for (const Case& c : cases) {
    const BinaryImage image = speckledPage(c.width, c.height, c.seed);
    const Dpi dpi(300, 300);
    DebugLabels steps;
    const uint64_t results[5] = {
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::CAUTIOUS, NullTaskStatus(), &steps)),
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::NORMAL, NullTaskStatus(), &steps)),
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::AGGRESSIVE, NullTaskStatus(), &steps)),
        pixelHash(Despeckle::despeckle(image, dpi, 1.5, NullTaskStatus(), &steps)),
        pixelHash(Despeckle::despeckle(image, dpi, 3.5, NullTaskStatus(), &steps)),
    };
    BOOST_CHECK(steps.labels.contains(kSecondChance));
    for (int i = 0; i < 5; ++i) {
      BOOST_CHECK_EQUAL(results[i], c.expected[i]);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
