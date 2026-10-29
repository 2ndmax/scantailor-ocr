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

// At NORMAL and 300 dpi: components of 12 px width or height are kept for sure, those of fewer
// than 2 px removed for sure. Any other reaches 8 * sqrt(area), at most 41.3 px, and needs a
// neighbour of at least 0.35 times its area.
BinaryImage despeckled(const BinaryImage& image) {
  return Despeckle::despeckle(image, Dpi(300, 300), Despeckle::NORMAL, NullTaskStatus());
}

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
  // y before x, each in a statement of its own: the order of function arguments is up to the
  // compiler, and the expected hashes below were recorded with y drawn first.
  for (int i = 0; i < area / 400; ++i) {
    const int w = 1 + next(next(4) == 0 ? 30 : 8);
    const int h = 1 + next(next(4) == 0 ? 30 : 8);
    const int y = next(height);
    const int x = next(width);
    image.fill(QRect(x, y, w, h) & image.rect(), BLACK);
  }
  for (int i = 0; i < area / 150; ++i) {
    const int y = next(height);
    const int x = next(width);
    image.setPixel(x, y, BLACK);
  }
  return image;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DespeckleTestSuite)

// A lone speck on the left, with nothing big to keep it.
BOOST_AUTO_TEST_CASE(speck_on_the_left_is_removed) {
  BinaryImage image(3, 1, WHITE);
  image.setPixel(0, 0, BLACK);
  BOOST_CHECK_EQUAL(toText(despeckled(image)).toStdString(), ".../");
}

// Components near each other but none big enough to be kept for sure: all go.
BOOST_AUTO_TEST_CASE(nothing_big_means_nothing_is_kept) {
  BinaryImage image(10, 3, WHITE);
  image.fill(QRect(0, 0, 3, 3), BLACK);
  image.fill(QRect(6, 1, 2, 1), BLACK);
  BOOST_CHECK_EQUAL(toText(despeckled(image)).toStdString(), "........../........../........../");
}

BOOST_AUTO_TEST_CASE(white_and_black_pages_stay_as_they_are) {
  const BinaryImage white(50, 20, WHITE);
  BOOST_CHECK(despeckled(white) == white);
  const BinaryImage black(50, 20, BLACK);
  BOOST_CHECK(despeckled(black) == black);
}

// Without a resolution there are no physical sizes; the image stays as it is.
BOOST_AUTO_TEST_CASE(image_without_resolution_is_not_changed) {
  BinaryImage image(10, 3, WHITE);
  image.setPixel(5, 1, BLACK);
  const BinaryImage result = Despeckle::despeckle(image, Dpi(0, 0), Despeckle::NORMAL, NullTaskStatus());
  BOOST_CHECK(result == image);
}

// A single pixel is too small to be anything, even right next to a letter; two pixels there stay.
BOOST_AUTO_TEST_CASE(single_pixel_next_to_text_is_removed) {
  BinaryImage image(30, 20, WHITE);
  image.fill(QRect(0, 0, 20, 20), BLACK);
  image.setPixel(21, 5, BLACK);
  image.fill(QRect(21, 12, 2, 1), BLACK);
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(21, 5) == WHITE);
  BOOST_CHECK(result.getPixel(21, 12) == BLACK);
}

// The two dots of an umlaut above the stem of a letter stay; a dot of the same size far away goes.
BOOST_AUTO_TEST_CASE(dots_above_a_letter_are_kept) {
  BinaryImage image(80, 30, WHITE);
  image.fill(QRect(10, 10, 3, 12), BLACK);
  image.fill(QRect(6, 4, 3, 3), BLACK);
  image.fill(QRect(14, 4, 3, 3), BLACK);
  image.fill(QRect(60, 10, 3, 3), BLACK);
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(7, 5) == BLACK);
  BOOST_CHECK(result.getPixel(15, 5) == BLACK);
  BOOST_CHECK(result.getPixel(61, 11) == WHITE);
}

// A dot leader after text is kept as a whole, each dot by the one before it. The same row of dots
// without text goes.
BOOST_AUTO_TEST_CASE(dot_leader_after_text_is_kept) {
  BinaryImage image(200, 70, WHITE);
  image.fill(QRect(0, 0, 12, 12), BLACK);
  for (int x = 22; x < 200; x += 12) {
    image.fill(QRect(x, 8, 3, 3), BLACK);
    image.fill(QRect(x, 60, 3, 3), BLACK);
  }
  const BinaryImage result = despeckled(image);
  for (int x = 22; x < 200; x += 12) {
    BOOST_CHECK_MESSAGE(result.getPixel(x, 8) == BLACK, "leader dot at x = " << x);
    BOOST_CHECK_MESSAGE(result.getPixel(x, 60) == WHITE, "lone dot at x = " << x);
  }
}

// A dot of a leader cut off from the dot before it by a bar too large for the dots to keep: it
// finds that dot by its distance, and the rest of the leader follows.
BOOST_AUTO_TEST_CASE(leader_dot_behind_a_large_speck_is_kept) {
  BinaryImage image(200, 19, WHITE);
  image.fill(QRect(0, 0, 12, 12), BLACK);
  for (int x = 22; x < 200; x += 12) {
    image.fill(QRect(x, 8, 3, 3), BLACK);
  }
  image.fill(QRect(64, 4, 3, 11), BLACK);  // between the dots at 58 and 70, out of the reach of the block
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(65, 8) == WHITE);
  for (int x = 22; x < 200; x += 12) {
    BOOST_CHECK_MESSAGE(result.getPixel(x, 8) == BLACK, "leader dot at x = " << x);
  }
}

// A component near a big block, but cut off from it by a wall of kept specks too small to keep it:
// it finds the block by its distance. Lines at the top and bottom keep the wall closed.
BOOST_AUTO_TEST_CASE(component_behind_a_wall_of_small_specks_is_kept) {
  BinaryImage image(40, 101, WHITE);
  image.fill(QRect(0, 0, 40, 1), BLACK);
  image.fill(QRect(0, 100, 40, 1), BLACK);
  image.fill(QRect(0, 30, 20, 41), BLACK);
  for (int y = 2; y <= 97; y += 3) {
    image.fill(QRect(24, y, 1, 2), BLACK);
  }
  image.fill(QRect(30, 46, 5, 8), BLACK);  // 40 px, 11 px from the block
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(24, 50) == BLACK);
  BOOST_CHECK(result.getPixel(32, 50) == BLACK);
}

// The reach grows with the size: 2 x 2 pixels reach 8 * 2 = 16 px.
BOOST_AUTO_TEST_CASE(speck_is_kept_up_to_its_reach) {
  for (const int gap : {16, 17}) {
    BinaryImage image(60, 20, WHITE);
    image.fill(QRect(0, 0, 20, 20), BLACK);
    image.fill(QRect(19 + gap, 9, 2, 2), BLACK);
    BOOST_CHECK_MESSAGE((despeckled(image).getPixel(19 + gap, 9) == BLACK) == (gap == 16), "gap " << gap);
  }
}

// ... but not beyond 3.5 mm, 41.3 px: 8 x 8 pixels would reach 64 px.
BOOST_AUTO_TEST_CASE(reach_is_capped) {
  for (const int gap : {41, 42}) {
    BinaryImage image(80, 20, WHITE);
    image.fill(QRect(0, 0, 20, 20), BLACK);
    image.fill(QRect(19 + gap, 6, 8, 8), BLACK);
    BOOST_CHECK_MESSAGE((despeckled(image).getPixel(19 + gap, 6) == BLACK) == (gap == 41), "gap " << gap);
  }
}

// The sizes are in millimetres, so in pixels they follow the resolution. The content detection
// despeckles at 150 dpi, the output mostly at 600 dpi. At NORMAL the reach is capped at 3.5 mm:
// 20.7 px at 150 dpi, 82.7 px at 600 dpi. The specks reach 8 * 5 = 40 px and 8 * 12 = 96 px
// uncapped, and stay below the size kept for sure (1.016 mm: 6 and 24 px).
BOOST_AUTO_TEST_CASE(reach_is_capped_in_millimetres_at_any_resolution) {
  struct Case {
    int dpi;
    int block;
    int speck;
    int lastKeptGap;
  };
  for (const Case& c : {Case{150, 20, 5, 20}, Case{600, 30, 12, 82}}) {
    for (const int gap : {c.lastKeptGap, c.lastKeptGap + 1}) {
      BinaryImage image(c.block + gap + c.speck + 5, c.block, WHITE);
      image.fill(QRect(0, 0, c.block, c.block), BLACK);
      image.fill(QRect(c.block - 1 + gap, 0, c.speck, c.speck), BLACK);
      const BinaryImage result = Despeckle::despeckle(image, Dpi(c.dpi, c.dpi), Despeckle::NORMAL, NullTaskStatus());
      BOOST_CHECK_MESSAGE((result.getPixel(c.block - 1 + gap, 0) == BLACK) == (gap == c.lastKeptGap),
                          c.dpi << " dpi, gap " << gap);
    }
  }
}

// Below 0.015 mm² a component is removed for sure, even right next to a letter: fewer than 1 px at
// 150 dpi, so a single pixel stays there, and fewer than 8 px at 600 dpi.
BOOST_AUTO_TEST_CASE(smallest_area_kept_follows_the_resolution) {
  struct Case {
    int dpi;
    int block;
    QSize removed;  // empty: none is removed
    QSize kept;
  };
  for (const Case& c : {Case{150, 20, QSize(), QSize(1, 1)}, Case{600, 30, QSize(2, 3), QSize(2, 4)}}) {
    BinaryImage image(c.block + 10, c.block, WHITE);
    image.fill(QRect(0, 0, c.block, c.block), BLACK);
    image.fill(QRect(QPoint(c.block + 2, 2), c.kept), BLACK);
    if (!c.removed.isEmpty()) {
      image.fill(QRect(QPoint(c.block + 2, 12), c.removed), BLACK);
    }
    const BinaryImage result = Despeckle::despeckle(image, Dpi(c.dpi, c.dpi), Despeckle::NORMAL, NullTaskStatus());
    BOOST_CHECK_MESSAGE(result.getPixel(c.block + 2, 2) == BLACK, c.dpi << " dpi: kept speck");
    if (!c.removed.isEmpty()) {
      BOOST_CHECK_MESSAGE(result.getPixel(c.block + 2, 12) == WHITE, c.dpi << " dpi: removed speck");
    }
  }
}

// Kept dots keep components of similar size, not much larger ones. These are too far from the block
// to be kept by it.
BOOST_AUTO_TEST_CASE(small_component_keeps_only_similar_ones) {
  for (const int size : {3, 6}) {
    BinaryImage image(80, 20, WHITE);
    image.fill(QRect(0, 0, 20, 20), BLACK);
    image.fill(QRect(35, 9, 2, 2), BLACK);        // 16 px from the block: kept
    image.fill(QRect(51, 9, 2, 2), BLACK);        // 14 px from the dot before: kept
    image.fill(QRect(67, 7, size, size), BLACK);  // 48 px from the block, 14 px from the dot before
    const BinaryImage result = despeckled(image);
    BOOST_CHECK(result.getPixel(51, 9) == BLACK);
    BOOST_CHECK_MESSAGE((result.getPixel(67, 7) == BLACK) == (size == 3), "size " << size);
  }
}

// A speck inside a closed ring, too far from it to be kept.
BOOST_AUTO_TEST_CASE(speck_inside_a_ring_is_removed) {
  BinaryImage image(30, 30, WHITE);
  image.fill(QRect(0, 0, 30, 30), BLACK);
  image.fill(QRect(2, 2, 26, 26), WHITE);
  image.fill(QRect(14, 15, 2, 1), BLACK);  // 13 px from the ring, reaches 11.3 px
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(14, 15) == WHITE);
  BOOST_CHECK(result.getPixel(0, 0) == BLACK);
}

// A line of "letters" 7 x 14 px on the baseline y = 49, every 20 px, with marks in the gaps between
// them: marks of text where they sit in text stay, specks elsewhere next to the letters go.
BOOST_AUTO_TEST_CASE(marks_next_to_letters_stay_where_text_has_them) {
  BinaryImage image(220, 70, WHITE);
  for (int x = 10; x < 200; x += 20) {
    if (x != 170) {  // a wide gap between words
      image.fill(QRect(x, 36, 7, 14), BLACK);
    }
  }
  image.fill(QRect(19, 48, 2, 2), BLACK);   // period on the baseline
  image.fill(QRect(39, 41, 2, 2), BLACK);   // speck at mid-height
  image.fill(QRect(59, 53, 2, 2), BLACK);   // speck below the baseline
  image.fill(QRect(80, 41, 6, 2), BLACK);   // hyphen
  image.fill(QRect(100, 40, 2, 2), BLACK);  // colon
  image.fill(QRect(100, 47, 2, 2), BLACK);
  image.fill(QRect(120, 48, 2, 5), BLACK);  // comma across the baseline
  image.fill(QRect(152, 31, 2, 2), BLACK);  // dot above a letter
  image.fill(QRect(172, 31, 2, 2), BLACK);  // speck above the gap between words
  const BinaryImage result = despeckled(image);
  BOOST_CHECK_MESSAGE(result.getPixel(19, 48) == BLACK, "period");
  BOOST_CHECK_MESSAGE(result.getPixel(39, 41) == WHITE, "speck at mid-height");
  BOOST_CHECK_MESSAGE(result.getPixel(59, 53) == WHITE, "speck below the baseline");
  BOOST_CHECK_MESSAGE(result.getPixel(80, 41) == BLACK, "hyphen");
  BOOST_CHECK_MESSAGE(result.getPixel(100, 40) == BLACK, "upper dot of the colon");
  BOOST_CHECK_MESSAGE(result.getPixel(100, 47) == BLACK, "lower dot of the colon");
  BOOST_CHECK_MESSAGE(result.getPixel(120, 48) == BLACK, "comma");
  BOOST_CHECK_MESSAGE(result.getPixel(152, 31) == BLACK, "dot above a letter");
  BOOST_CHECK_MESSAGE(result.getPixel(172, 31) == WHITE, "speck above a gap");
}

// Without a line of letters nearby (fewer than three), there is nothing to judge the position by:
// the speck at mid-height beside a single letter stays.
BOOST_AUTO_TEST_CASE(speck_beside_a_lone_letter_stays) {
  BinaryImage image(60, 70, WHITE);
  image.fill(QRect(10, 36, 7, 14), BLACK);
  image.fill(QRect(30, 36, 7, 14), BLACK);
  image.fill(QRect(19, 41, 2, 2), BLACK);
  BOOST_CHECK(despeckled(image).getPixel(19, 41) == BLACK);
}

// Three lines of 10 letters on the left; at the right edge a clump of dirt, big enough to be kept,
// outside the text and on no line. Specks of 3 x 3 px reach 24 px, next to such a clump only 9.6.
BOOST_AUTO_TEST_CASE(clump_off_the_text_keeps_specks_only_close_by) {
  BinaryImage image(500, 120, WHITE);
  for (int baseline : {30, 60, 90}) {
    for (int x = 20; x < 140; x += 12) {
      image.fill(QRect(x, baseline - 13, 7, 14), BLACK);
    }
  }
  image.fill(QRect(420, 50, 14, 14), BLACK);
  image.fill(QRect(442, 55, 3, 3), BLACK);  // 9 px from the clump
  image.fill(QRect(398, 55, 3, 3), BLACK);  // 20 px from the clump
  const BinaryImage result = despeckled(image);
  BOOST_CHECK(result.getPixel(420, 50) == BLACK);
  BOOST_CHECK(result.getPixel(442, 55) == BLACK);
  BOOST_CHECK(result.getPixel(398, 55) == WHITE);
}

BOOST_AUTO_TEST_CASE(debug_images_show_the_steps) {
  BinaryImage image(30, 20, WHITE);
  image.fill(QRect(0, 0, 20, 20), BLACK);
  image.fill(QRect(25, 5, 2, 2), BLACK);
  DebugLabels steps;
  Despeckle::despeckle(image, Dpi(300, 300), Despeckle::NORMAL, NullTaskStatus(), &steps);
  BOOST_CHECK_EQUAL(steps.labels.join(u',').toStdString(), "components,voronoi,despeckled");
}

// Characterization: the results of the despeckling on generated pages, so that any change of them
// is deliberate.
BOOST_AUTO_TEST_CASE(results_are_unchanged) {
  struct Case {
    int width;
    int height;
    uint32_t seed;
    uint64_t expected[5];  // CAUTIOUS, NORMAL, AGGRESSIVE, level 0.5, level 3.5
  };
  const Case cases[] = {
      {97,
       61,
       1,
       {17881253642145317734ULL, 8956172799035862734ULL, 776708475556728212ULL, 18355458573948367603ULL,
        776708475556728212ULL}},
      {256,
       193,
       2,
       {4066094122314015042ULL, 9507686487759242760ULL, 10685311043735323495ULL, 5497798018484983095ULL,
        10685311043735323495ULL}},
      {400,
       300,
       3,
       {2172329608748404732ULL, 18168882280203925951ULL, 10603863443824478450ULL, 1467042386336497495ULL,
        11239362042837949490ULL}},
  };
  for (const Case& c : cases) {
    const BinaryImage image = speckledPage(c.width, c.height, c.seed);
    const Dpi dpi(300, 300);
    const uint64_t results[5] = {
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::CAUTIOUS, NullTaskStatus())),
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::NORMAL, NullTaskStatus())),
        pixelHash(Despeckle::despeckle(image, dpi, Despeckle::AGGRESSIVE, NullTaskStatus())),
        pixelHash(Despeckle::despeckle(image, dpi, 0.5, NullTaskStatus())),
        pixelHash(Despeckle::despeckle(image, dpi, 3.5, NullTaskStatus())),
    };
    for (int i = 0; i < 5; ++i) {
      BOOST_CHECK_EQUAL(results[i], c.expected[i]);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
