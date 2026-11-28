// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BWColor.h>
#include <BinaryImage.h>
#include <ConnectivityMap.h>
#include <InfluenceMap.h>

#include <QImage>
#include <boost/test/unit_test.hpp>
#include <random>

#include "Utils.h"

namespace imageproc {
namespace tests {
using namespace utils;

BOOST_AUTO_TEST_SUITE(BinaryImageTestSuite)

BOOST_AUTO_TEST_CASE(test_null_image) {
  BOOST_CHECK(BinaryImage().toQImage() == QImage());
  BOOST_CHECK(BinaryImage(QImage()).toQImage() == QImage());
}

BOOST_AUTO_TEST_CASE(test_from_to_qimage) {
  const int w = 50;
  const int h = 64;
  QImage qimgArgb32(w, h, QImage::Format_ARGB32);
  QImage qimgMono(w, h, QImage::Format_Mono);
  qimgMono.setColorCount(2);
  qimgMono.setColor(0, 0xffffffff);
  qimgMono.setColor(1, 0xff000000);
  std::mt19937 rng(0x5CA1E001u);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const unsigned rnd = rng() & 1u;
      qimgArgb32.setPixel(x, y, rnd ? 0x66888888 : 0x66777777);
      qimgMono.setPixel(x, y, rnd ? 0 : 1);
    }
  }

  QImage qimgMonoLsb(qimgMono.convertToFormat(QImage::Format_MonoLSB));
  QImage qimgRgb32(qimgArgb32.convertToFormat(QImage::Format_RGB32));
  QImage qimgArgb32Pm(qimgArgb32.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  QImage qimgIndexed8(qimgRgb32.convertToFormat(QImage::Format_Indexed8));

  BOOST_REQUIRE(BinaryImage(qimgMono).toQImage() == qimgMono);
  BOOST_CHECK(BinaryImage(qimgMonoLsb).toQImage() == qimgMono);
  BOOST_CHECK(BinaryImage(qimgArgb32).toQImage() == qimgMono);
  BOOST_CHECK(BinaryImage(qimgRgb32).toQImage() == qimgMono);
  BOOST_CHECK(BinaryImage(qimgArgb32Pm).toQImage() == qimgMono);
  BOOST_CHECK(BinaryImage(qimgIndexed8).toQImage() == qimgMono);
}

BOOST_AUTO_TEST_CASE(test_full_fill) {
  BinaryImage white(100, 100);
  white.fill(WHITE);

  QImage qWhite(100, 100, QImage::Format_Mono);
  qWhite.setColorCount(2);
  qWhite.setColor(0, 0xff000000);
  qWhite.setColor(1, 0xffffffff);
  qWhite.fill(1);

  BOOST_REQUIRE(BinaryImage(qWhite) == white);

  BinaryImage black(30, 30);
  black.fill(BLACK);

  QImage qBlack(30, 30, QImage::Format_Mono);
  qBlack.setColorCount(2);
  qBlack.setColor(0, 0xff000000);
  qBlack.setColor(1, 0xffffffff);
  qBlack.fill(0);

  BOOST_REQUIRE(BinaryImage(qBlack) == black);
}

BOOST_AUTO_TEST_CASE(test_partial_fill_small) {
  QImage qImage(randomMonoQImage(100, 100));

  const QRect rect(80, 80, 20, 20);
  BinaryImage image(qImage);
  image.fill(rect, WHITE);
  QImage whiteRect(rect.width(), rect.height(), QImage::Format_Mono);
  whiteRect.setColorCount(2);
  whiteRect.setColor(0, 0xffffffff);
  whiteRect.setColor(1, 0xff000000);
  whiteRect.fill(0);
  BOOST_REQUIRE(image.toQImage().copy(rect) == whiteRect);
  BOOST_CHECK(surroundingsIntact(image.toQImage(), qImage, rect));
}

BOOST_AUTO_TEST_CASE(test_partial_fill_large) {
  QImage qImage(randomMonoQImage(100, 100));

  const QRect rect(20, 20, 79, 79);
  BinaryImage image(qImage);
  image.fill(rect, WHITE);
  QImage whiteRect(rect.width(), rect.height(), QImage::Format_Mono);
  whiteRect.setColorCount(2);
  whiteRect.setColor(0, 0xffffffff);
  whiteRect.setColor(1, 0xff000000);
  whiteRect.fill(0);
  BOOST_REQUIRE(image.toQImage().copy(rect) == whiteRect);
  BOOST_CHECK(surroundingsIntact(image.toQImage(), qImage, rect));
}

BOOST_AUTO_TEST_CASE(test_fill_except) {
  QImage qImage(randomMonoQImage(100, 100));

  const QRect rect(20, 20, 79, 79);
  BinaryImage image(qImage);
  image.fillExcept(rect, BLACK);

  QImage blackImage(qImage.width(), qImage.height(), QImage::Format_Mono);
  blackImage.setColorCount(2);
  blackImage.setColor(0, 0xffffffff);
  blackImage.setColor(1, 0xff000000);
  blackImage.fill(1);

  BOOST_REQUIRE(image.toQImage().copy(rect) == qImage.copy(rect));
  BOOST_CHECK(surroundingsIntact(image.toQImage(), blackImage, rect));
}

// A part of a 16 bit image is binarized from its own position, not from the top left corner.
BOOST_AUTO_TEST_CASE(test_part_of_rgb16_image) {
  QImage image(100, 100, QImage::Format_RGB16);
  image.fill(Qt::white);
  for (int y = 50; y < 60; ++y) {
    for (int x = 50; x < 60; ++x) {
      image.setPixel(x, y, qRgb(0, 0, 0));
    }
  }
  const BinaryImage part(image, QRect(50, 50, 10, 10));
  BOOST_REQUIRE(part.size() == QSize(10, 10));
  BOOST_CHECK_EQUAL(part.countBlackPixels(), 100);

  const BinaryImage corner(image, QRect(0, 0, 10, 10));
  BOOST_CHECK_EQUAL(corner.countBlackPixels(), 0);
}

BOOST_AUTO_TEST_CASE(test_content_bounding_box4) {
  static const int inp[]
      = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,
         0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

  const BinaryImage img(makeBinaryImage(inp, 9, 8));
  BOOST_CHECK(img.contentBoundingBox() == QRect(1, 1, 6, 6));
}

BOOST_AUTO_TEST_CASE(test_connectivity_map_from_influence_map) {
  BinaryImage seed(8, 8, WHITE);
  seed.fill(QRect(1, 1, 2, 2), BLACK);
  seed.fill(QRect(5, 5, 2, 2), BLACK);
  const ConnectivityMap initial(seed, CONN4);
  const InfluenceMap imap(initial);

  const ConnectivityMap copied(imap);
  BOOST_REQUIRE(copied.data() != nullptr);
  BOOST_CHECK_EQUAL(copied.stride(), copied.size().width() + 2);
  BOOST_CHECK_EQUAL(copied.maxLabel(), imap.maxLabel());
  BOOST_CHECK_EQUAL(copied.data()[1 * copied.stride() + 1], imap.data()[1 * imap.stride() + 1].label);
  BOOST_CHECK_EQUAL(copied.data()[5 * copied.stride() + 5], imap.data()[5 * imap.stride() + 5].label);

  // Fast-path assignment on matching size must update maxLabel() and pixel labels.
  ConnectivityMap assigned(QSize(8, 8));
  BOOST_REQUIRE_EQUAL(assigned.maxLabel(), 0u);
  assigned = imap;
  BOOST_REQUIRE(assigned.data() != nullptr);
  BOOST_CHECK_EQUAL(assigned.maxLabel(), imap.maxLabel());
  BOOST_CHECK_EQUAL(assigned.data()[5 * assigned.stride() + 5], imap.data()[5 * imap.stride() + 5].label);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc