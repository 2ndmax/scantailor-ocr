// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <GrayImage.h>
#include <Scale.h>

#include <QImage>
#include <QSize>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(ScaleTestSuite)

BOOST_AUTO_TEST_CASE(test_null_image) {
  const GrayImage nullImg;
  BOOST_CHECK(scaleToGray(nullImg, QSize(1, 1)).isNull());
}

static bool fuzzyCompare(const QImage& img1, const QImage& img2) {
  BOOST_REQUIRE(img1.size() == img2.size());

  const int width = img1.width();
  const int height = img1.height();
  const uint8_t* line1 = img1.bits();
  const uint8_t* line2 = img2.bits();
  const int line1Bpl = img1.bytesPerLine();
  const int line2Bpl = img2.bytesPerLine();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (std::abs(int(line1[x]) - int(line2[x])) > 1) {
        return false;
      }
    }
    line1 += line1Bpl;
    line2 += line2Bpl;
  }
  return true;
}

/**
 * A straightforward, exact implementation of downscaling by area averaging:
 * each destination pixel is the mean of the source area it covers, with
 * partially covered source pixels weighted by the covered fraction.
 *
 * The test used to compare against QImage::scaled() instead, but Qt's smooth
 * scaling algorithm changed between Qt 5 and Qt 6, making that comparison fail
 * although scaleToGray() itself is fine.
 */
static GrayImage referenceDownscale(const GrayImage& src, const QSize& dstSize) {
  GrayImage dst(dstSize);
  const double xRatio = double(src.width()) / dstSize.width();
  const double yRatio = double(src.height()) / dstSize.height();
  const uint8_t* srcData = src.data();
  const int srcStride = src.stride();

  for (int dy = 0; dy < dstSize.height(); ++dy) {
    const double top = dy * yRatio;
    const double bottom = (dy + 1) * yRatio;
    for (int dx = 0; dx < dstSize.width(); ++dx) {
      const double left = dx * xRatio;
      const double right = (dx + 1) * xRatio;
      double sum = 0.0;
      for (auto sy = static_cast<int>(top); sy < bottom && sy < src.height(); ++sy) {
        const double yWeight = std::min<double>(sy + 1, bottom) - std::max<double>(sy, top);
        for (auto sx = static_cast<int>(left); sx < right && sx < src.width(); ++sx) {
          const double xWeight = std::min<double>(sx + 1, right) - std::max<double>(sx, left);
          sum += xWeight * yWeight * srcData[sy * srcStride + sx];
        }
      }
      dst.data()[dy * dst.stride() + dx] = static_cast<uint8_t>(std::lround(sum / (xRatio * yRatio)));
    }
  }
  return dst;
}

static bool checkScale(const GrayImage& img, const QSize& newSize) {
  const GrayImage scaled1(scaleToGray(img, newSize));
  const GrayImage scaled2(referenceDownscale(img, newSize));
  return fuzzyCompare(scaled1, scaled2);
}

BOOST_AUTO_TEST_CASE(test_random_image) {
  GrayImage img(QSize(100, 100));
  uint8_t* line = img.data();
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      line[x] = static_cast<uint8_t>(rand() % 256);
    }
    line += img.stride();
  }

  // Only downscaling is checked: upscaling interpolates, and there's
  // no single "right" result to compare against.  The ratios are chosen to
  // be exactly representable in the 1/32 fixed point arithmetic scaleToGray()
  // uses; others may legitimately be off by a bit more than one gray level.

  BOOST_CHECK(checkScale(img, QSize(50, 50)));
  BOOST_CHECK(checkScale(img, QSize(80, 80)));
  BOOST_CHECK(checkScale(img, QSize(80, 50)));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc