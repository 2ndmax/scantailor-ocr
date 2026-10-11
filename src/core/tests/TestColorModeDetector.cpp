// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ColorModeDetector.h>
#include <NullTaskStatus.h>

#include <QImage>
#include <QPainter>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>

namespace Tests {
namespace {
// Synthetic pages at 300 dpi, A5-like.
const int PAGE_WIDTH = 1240;
const int PAGE_HEIGHT = 1750;

class Random {
 public:
  explicit Random(uint32_t seed) : m_state(seed) {}

  int next(int range) {
    m_state = m_state * 1664525u + 1013904223u;
    return static_cast<int>((m_state >> 8) % static_cast<uint32_t>(range));
  }

 private:
  uint32_t m_state;
};

QImage blankPage() {
  QImage page(PAGE_WIDTH, PAGE_HEIGHT, QImage::Format_RGB32);
  page.fill(QColor(250, 250, 248));
  const int dotsPerMeter = qRound(300 / 0.0254);
  page.setDotsPerMeterX(dotsPerMeter);
  page.setDotsPerMeterY(dotsPerMeter);
  return page;
}

// Lines of "letters" (small dark bars) between top and bottom, in the given color.
void drawText(QImage& page, int top, int bottom, const QColor& color, uint32_t seed) {
  QPainter painter(&page);
  Random random(seed);
  for (int y = top; y + 22 < bottom; y += 40) {
    for (int x = 120; x < PAGE_WIDTH - 140;) {
      const int letterWidth = 8 + random.next(12);
      painter.fillRect(x, y + random.next(4), letterWidth, 18 + random.next(6), color);
      x += letterWidth + 4 + ((random.next(7) == 0) ? 18 : 0);
    }
  }
}

// A continuous-tone picture: smooth shapes plus grain, in gray or in color.
void drawPicture(QImage& page, const QRect& rect, bool color, uint32_t seed) {
  Random random(seed);
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(page.scanLine(y));
    for (int x = rect.left(); x <= rect.right(); ++x) {
      const double base = 120 + 70 * std::sin(x / 41.0) * std::cos(y / 29.0) + 30 * std::sin((x + y) / 97.0);
      const int v = qBound(0, static_cast<int>(base) + random.next(41) - 20, 255);
      if (color) {
        line[x] = qRgb(qBound(0, v + 60, 255), qBound(0, v - 30, 255), qBound(0, v / 2, 255));
      } else {
        line[x] = qRgb(v, v, v);
      }
    }
  }
}

ColorModeDetector::Mode detect(const QImage& page, bool zones = false) {
  ColorModeDetector::Options options;
  options.usePictureZones = zones;
  ColorModeDetector::Evidence ev;
  const ColorModeDetector::Mode mode = ColorModeDetector::detect(page, options, NullTaskStatus(), &ev);
  BOOST_TEST_MESSAGE(ColorModeDetector::modeToString(mode)
                     << " " << ev.reason.toStdString()
                     << " spectre=" << LeptonicaDetector::colorTypeToString(ev.spectreVerdict)
                     << " colorOutside=" << ev.colorOutside << " colorInPictures=" << ev.colorInPictures
                     << " largest=" << ev.largestPicture << " dark=" << ev.spectre.darkRatio);
  return mode;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ColorModeDetectorTestSuite)

BOOST_AUTO_TEST_CASE(text_page_is_black_and_white) {
  QImage page = blankPage();
  drawText(page, 120, PAGE_HEIGHT - 120, QColor(20, 20, 20), 1);
  BOOST_CHECK(detect(page) == ColorModeDetector::Mode::BlackWhite);
  BOOST_CHECK(detect(page, true) == ColorModeDetector::Mode::BlackWhite);
}

BOOST_AUTO_TEST_CASE(text_with_gray_photo_is_mixed) {
  QImage page = blankPage();
  drawText(page, 120, 700, QColor(20, 20, 20), 2);
  drawPicture(page, QRect(160, 760, 900, 640), false, 3);
  drawText(page, 1450, PAGE_HEIGHT - 120, QColor(20, 20, 20), 4);
  BOOST_CHECK(detect(page, true) == ColorModeDetector::Mode::Mixed);
}

BOOST_AUTO_TEST_CASE(colored_headings_alone_do_not_make_a_color_page) {
  QImage page = blankPage();
  for (int i = 0; i < 4; ++i) {
    const int top = 120 + i * 400;
    drawText(page, top, top + 45, QColor(25, 45, 150), 10 + i);  // Dark blue, as printed headings are.
    drawText(page, top + 60, top + 380, QColor(20, 20, 20), 20 + i);
  }
  BOOST_CHECK(detect(page) == ColorModeDetector::Mode::BlackWhite);
}

BOOST_AUTO_TEST_CASE(text_with_color_photo_is_mixed) {
  QImage page = blankPage();
  drawText(page, 120, 700, QColor(20, 20, 20), 5);
  drawPicture(page, QRect(160, 760, 900, 640), true, 6);
  drawText(page, 1450, PAGE_HEIGHT - 120, QColor(20, 20, 20), 7);
  BOOST_CHECK(detect(page) == ColorModeDetector::Mode::Mixed);
}

BOOST_AUTO_TEST_CASE(much_color_outside_pictures_is_color) {
  QImage page = blankPage();
  drawText(page, 120, PAGE_HEIGHT - 120, QColor(190, 30, 30), 8);
  BOOST_CHECK(detect(page) == ColorModeDetector::Mode::Color);
}

BOOST_AUTO_TEST_CASE(picture_zones_find_a_light_photo) {
  QImage page = blankPage();
  drawText(page, 120, 700, QColor(20, 20, 20), 9);
  // Light and low in contrast, like the photos the Spectre detection missed.
  QImage photo = blankPage();
  drawPicture(photo, photo.rect(), false, 10);
  QPainter painter(&page);
  painter.setOpacity(0.6);
  painter.drawImage(QRect(160, 760, 900, 640), photo, QRect(0, 0, 900, 640));
  painter.end();
  drawText(page, 1450, PAGE_HEIGHT - 120, QColor(20, 20, 20), 11);
  BOOST_CHECK(detect(page, true) == ColorModeDetector::Mode::Mixed);
}

BOOST_AUTO_TEST_CASE(large_photo_with_caption_is_mixed) {
  QImage page = blankPage();
  drawPicture(page, QRect(100, 100, PAGE_WIDTH - 200, PAGE_HEIGHT - 320), false, 13);
  drawText(page, PAGE_HEIGHT - 190, PAGE_HEIGHT - 120, QColor(20, 20, 20), 14);
  BOOST_CHECK(detect(page) == ColorModeDetector::Mode::Mixed);
}

BOOST_AUTO_TEST_CASE(dark_page_is_not_mixed) {
  QImage page = blankPage();
  page.fill(QColor(15, 15, 15));
  BOOST_CHECK(detect(page) != ColorModeDetector::Mode::Mixed);
}

BOOST_AUTO_TEST_CASE(nearly_empty_page_stays_black_and_white) {
  QImage page = blankPage();
  // Faint show-through from the back of the sheet.
  drawText(page, 120, PAGE_HEIGHT - 120, QColor(232, 232, 230), 12);
  BOOST_CHECK(detect(page, true) == ColorModeDetector::Mode::BlackWhite);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
