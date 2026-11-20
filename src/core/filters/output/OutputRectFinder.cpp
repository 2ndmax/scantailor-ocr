// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputRectFinder.h"

#include <cstdint>

using namespace imageproc;

namespace output::detail {
namespace {
const int MultiplyDeBruijnBitPosition[32] = {0,  1,  28, 2,  29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4,  8,
                                             31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6,  11, 5,  10, 9};

const int MultiplyDeBruijnBitPosition2[32] = {0, 9,  1,  10, 13, 21, 2,  29, 11, 14, 16, 18, 22, 25, 3, 30,
                                              8, 12, 20, 28, 15, 17, 24, 7,  19, 27, 23, 6,  26, 5,  4, 31};

/**
 * aka "Count the consecutive zero bits (trailing) on the right with multiply and lookup"
 * from Bit Twiddling Hacks By Sean Eron Anderson
 *
 * https://graphics.stanford.edu/~seander/bithacks.html#ZerosOnRightMultLookup
 */
inline int countConsecutiveZeroBitsTrailing(uint32_t v) {
  // 0u - v instead of -signed(v): the same bits, but defined also for v = 0x80000000.
  // REVIEW-NOTE (2026-10): no unit test for v = 0x80000000 -> a return to -signed(v) would go
  // unnoticed with MSVC. Not fixed because only UBSan sees the difference: analysis run 09 with
  // trimming_rect_picture_zones_stays_inside_the_area covers it.
  return MultiplyDeBruijnBitPosition[((v & (0u - v)) * 0x077CB531U) >> 27];
}

/**
 * aka "Find the log base 2 of an N-bit integer in O(lg(N)) operations with multiply and lookup"
 * from Bit Twiddling Hacks By Sean Eron Anderson
 *
 * https://graphics.stanford.edu/~seander/bithacks.html#IntegerLogDeBruijn
 */
inline int findPositionOfTheHighestBitSet(uint32_t v) {
  v |= v >> 1;  // first round down to one less than a power of 2
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return MultiplyDeBruijnBitPosition2[(uint32_t) (v * 0x07C4ACDDU) >> 27];
}
}  // namespace

std::vector<QRect> findRectAreas(const BinaryImage& mask, const int sensitivity) {
  if (mask.isNull()) {
    return {};
  }

  std::vector<QRect> areas;

  const int w = mask.width();
  const int h = mask.height();
  const int wpl = mask.wordsPerLine();
  const int lastWordIdx = (w - 1) >> 5;
  const int lastWordBits = w - (lastWordIdx << 5);
  const int lastWordUnusedBits = 32 - lastWordBits;
  const uint32_t lastWordMask = ~uint32_t(0) << lastWordUnusedBits;
  const uint32_t* const data = mask.data();

  const uint32_t* line = data;
  // create list of filled continuous blocks on each line
  for (int y = 0; y < h; ++y, line += wpl) {
    QRect area;
    area.setTop(y);
    area.setBottom(y);
    bool areaFound = false;
    for (int i = 0; i <= lastWordIdx; ++i) {
      uint32_t word = ~line[i];  // white pixels are the content
      if (i == lastWordIdx) {
        // The last (possibly incomplete) word.
        word &= lastWordMask;
      }
      if (word) {
        if (!areaFound) {
          area.setLeft((i << 5) + 31 - findPositionOfTheHighestBitSet(~line[i]));
          areaFound = true;
        }
        area.setRight(((i + 1) << 5) - 1);
      } else {
        if (areaFound) {
          uint32_t v = line[i - 1];
          if (v) {
            area.setRight(area.right() - countConsecutiveZeroBitsTrailing(~v));
          }
          areas.emplace_back(area);
          areaFound = false;
        }
      }
    }
    if (areaFound) {
      // The padding bits behind the width may be 0 (white); they must not count as content.
      uint32_t v = line[lastWordIdx] | ~lastWordMask;
      if (v) {
        area.setRight(area.right() - countConsecutiveZeroBitsTrailing(~v));
      }
      areas.emplace_back(area);
    }
  }

  // join adjacent blocks of areas
  bool join = true;
  int overlap = 16;
  while (join) {
    join = false;
    std::vector<QRect> tmp;
    for (QRect area : areas) {
      // take an area and try to join with something in tmp
      QRect enlArea(area.adjusted(-overlap, -overlap, overlap, overlap));
      bool intersected = false;
      std::vector<QRect> tmp2;
      for (QRect ta : tmp) {
        QRect enlTA(ta.adjusted(-overlap, -overlap, overlap, overlap));
        if (enlArea.intersects(enlTA)) {
          intersected = true;
          join = true;
          tmp2.push_back(area.united(ta));
        } else {
          tmp2.push_back(ta);
        }
      }
      if (!intersected) {
        tmp2.push_back(area);
      }
      tmp = tmp2;
    }
    areas = tmp;
  }

  const auto percent = (float) (sensitivity / 100.);
  if (percent < 1.) {
    for (QRect& area : areas) {
      int wordWidth = area.width() >> 5;

      // All bounds inclusive.
      const int left = area.left();
      const int leftWord = left >> 5;
      const int right = area.right();
      const int rightWord = right >> 5;
      const int top = area.top();
      const int bottom = area.bottom();

      const uint32_t* pdata = mask.data();

      // A column is counted over the rows top to bottom, so its share is one of the height.
      const auto columnCriterium = (int) ((bottom - top + 1) * percent);
      const auto criteriumWord = (int) (wordWidth * percent);

      // cut the dirty upper lines
      for (int y = top; y <= bottom; y++) {
        line = pdata + wpl * y;

        int mword = 0;

        for (int k = leftWord; k <= rightWord; k++) {
          if (!line[k]) {
            mword++;  // count the totally white words
          }
        }

        if (mword > criteriumWord) {
          area.setTop(y);
          break;
        }
      }

      // cut the dirty bottom lines
      for (int y = bottom; y >= top; y--) {
        line = pdata + wpl * y;

        int mword = 0;

        for (int k = leftWord; k <= rightWord; k++) {
          if (!line[k]) {
            mword++;
          }
        }

        if (mword > criteriumWord) {
          area.setBottom(y);
          break;
        }
      }

      for (int x = left; x <= right; x++) {
        int mword = 0;

        for (int y = top; y <= bottom; y++) {
          if (WHITE == mask.getPixel(x, y)) {
            mword++;
          }
        }

        if (mword > columnCriterium) {
          area.setLeft(x);
          break;
        }
      }

      for (int x = right; x >= left; x--) {
        int mword = 0;

        for (int y = top; y <= bottom; y++) {
          if (WHITE == mask.getPixel(x, y)) {
            mword++;
          }
        }

        if (mword > columnCriterium) {
          area.setRight(x);
          break;
        }
      }
    }
  }
  for (QRect& area : areas) {
    area = area.intersected(mask.rect());
  }
  return areas;
}
}  // namespace output::detail
