// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Grayscale.h>
#include <Transform.h>

#include <QImage>
#include <QSize>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <random>
#include <stdexcept>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(TransformTestSuite)

BOOST_AUTO_TEST_CASE(test_null_image) {
  const QImage nullImg;
  const QTransform nullXform;
  const QRect unitRect(0, 0, 1, 1);
  const QColor bgcolor(0xff, 0xff, 0xff);
  const OutsidePixels outsidePixels(OutsidePixels::assumeColor(bgcolor));
  BOOST_CHECK(transformToGray(nullImg, nullXform, unitRect, outsidePixels).isNull());
}

BOOST_AUTO_TEST_CASE(test_random_image) {
  GrayImage img(QSize(100, 100));
  uint8_t* line = img.data();
  std::mt19937 rng(0x5CA1E006u);
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      line[x] = static_cast<uint8_t>(rng() % 256u);
    }
    line += img.stride();
  }

  const QColor bgcolor(0xff, 0xff, 0xff);
  const OutsidePixels outsidePixels(OutsidePixels::assumeColor(bgcolor));

  const QTransform nullXform;
  BOOST_CHECK(transformToGray(img, nullXform, img.rect(), outsidePixels) == img);
}

BOOST_AUTO_TEST_CASE(test_transparent_gray_palette_keeps_alpha) {
  // A gray palette with a transparent entry: allGray() is true, but the alpha must not get lost.
  QImage img(2, 1, QImage::Format_Indexed8);
  img.setColorTable({qRgba(0, 0, 0, 0), qRgb(255, 255, 255)});
  img.setPixel(0, 0, 0);
  img.setPixel(1, 0, 1);

  const OutsidePixels outsidePixels(OutsidePixels::assumeColor(QColor(0xff, 0xff, 0xff)));
  const QImage dst(transform(img, QTransform(), img.rect(), outsidePixels));

  BOOST_REQUIRE(dst.hasAlphaChannel());
  BOOST_CHECK_EQUAL(qAlpha(dst.pixel(0, 0)), 0);
  BOOST_CHECK_EQUAL(qAlpha(dst.pixel(1, 0)), 0xff);
  BOOST_CHECK_EQUAL(qGray(dst.pixel(1, 0)), 0xff);
}

BOOST_AUTO_TEST_CASE(test_large_downscale_does_not_overflow) {
  const OutsidePixels outsidePixels(OutsidePixels::assumeColor(QColor(255, 255, 255)));
  const QTransform scaleXform(QTransform::fromScale(1.0 / 150.0, 1.0 / 150.0));
  const QRect dstRect(0, 0, 2, 2);

  GrayImage graySrc(QSize(300, 300));
  graySrc.fill(255);
  const GrayImage grayDst(transformToGray(graySrc, scaleXform, dstRect, outsidePixels));
  for (int y = 0; y < grayDst.height(); ++y) {
    for (int x = 0; x < grayDst.width(); ++x) {
      BOOST_REQUIRE_EQUAL(int(grayDst.data()[y * grayDst.stride() + x]), 255);
    }
  }

  QImage rgbSrc(300, 300, QImage::Format_RGB32);
  rgbSrc.fill(qRgb(255, 255, 255));
  const QImage rgbDst(transform(rgbSrc, scaleXform, dstRect, outsidePixels));
  for (int y = 0; y < rgbDst.height(); ++y) {
    for (int x = 0; x < rgbDst.width(); ++x) {
      BOOST_REQUIRE_EQUAL(rgbDst.pixel(x, y), qRgb(255, 255, 255));
    }
  }
}

BOOST_AUTO_TEST_CASE(test_transform_rejects_singular_and_handles_extreme_zoom) {
  GrayImage src(QSize(16, 16));
  src.fill(200);
  const OutsidePixels outsidePixels(OutsidePixels::assumeColor(QColor(255, 255, 255)));

  const QTransform singular(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
  BOOST_CHECK_THROW(transformToGray(src, singular, QRect(0, 0, 8, 8), outsidePixels), std::invalid_argument);
  BOOST_CHECK_THROW(transform(src.toQImage(), singular, QRect(0, 0, 8, 8), outsidePixels), std::invalid_argument);

  const GrayImage zoomedOut = transformToGray(src, QTransform::fromScale(1e-9, 1e-9), QRect(0, 0, 4, 4), outsidePixels);
  BOOST_CHECK(zoomedOut.size() == QSize(4, 4));

  const GrayImage farTranslated
      = transformToGray(src, QTransform::fromTranslate(1e9, 1e9), QRect(0, 0, 4, 4), outsidePixels);
  BOOST_CHECK(farTranslated.size() == QSize(4, 4));
  BOOST_CHECK_EQUAL(int(farTranslated.data()[0]), 255);
}

BOOST_AUTO_TEST_CASE(test_geometric_translation_rotation_and_scale) {
  QImage src(4, 4, QImage::Format_RGB32);
  src.fill(qRgb(20, 40, 60));
  src.setPixel(1, 0, qRgb(200, 10, 10));

  const QImage translated = transform(src, QTransform::fromTranslate(2.0, 1.0), QRect(0, 0, 6, 5),
                                      OutsidePixels::assumeColor(QColor(255, 255, 255)));
  BOOST_CHECK_EQUAL(translated.pixel(3, 1), qRgb(200, 10, 10));
  BOOST_CHECK_EQUAL(translated.pixel(0, 0), qRgb(255, 255, 255));

  const QImage nearestTranslated
      = transform(src, QTransform::fromTranslate(2.0, 1.0), QRect(0, 0, 6, 5), OutsidePixels::assumeWeakNearest());
  BOOST_CHECK_EQUAL(nearestTranslated.pixel(0, 0), qRgb(20, 40, 60));

  // 90 deg clockwise around origin maps (1, 0) -> (0, 1); shift by +3 in y into [0, 4)x[0, 4).
  GrayImage graySrc(QSize(4, 4));
  graySrc.fill(50);
  graySrc.data()[0 * graySrc.stride() + 1] = 220;
  const GrayImage rotated = transformToGray(graySrc, QTransform().rotate(90.0), QRect(-3, 0, 4, 4),
                                            OutsidePixels::assumeColor(QColor(0, 0, 0)));
  BOOST_CHECK_EQUAL(int(rotated.data()[1 * rotated.stride() + 2]), 220);

  const GrayImage upscaled = transformToGray(graySrc, QTransform::fromScale(2.0, 2.0), QRect(0, 0, 8, 8),
                                             OutsidePixels::assumeWeakNearest(), QSizeF(0.1, 0.1));
  BOOST_CHECK_EQUAL(int(upscaled.data()[0 * upscaled.stride() + 2]), 220);
  BOOST_CHECK_EQUAL(int(upscaled.data()[1 * upscaled.stride() + 3]), 220);
  BOOST_CHECK_EQUAL(int(upscaled.data()[4 * upscaled.stride() + 4]), 50);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc