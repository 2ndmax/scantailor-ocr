#include <OrientationDetector.h>

#include <Dpi.h>
#include <Dpm.h>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace {
QImage makeTextPage(int rotation) {
  QImage base(800, 2100, QImage::Format_RGB32);
  base.fill(Qt::white);
  {
    QPainter p(&base);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::black);
    for (int line = 0; line < 45; ++line) {
      int y = 50 + line * 40;
      for (int g = 0; g < 38; ++g) {
        int x = 60 + g * 18;
        // stem (x-height)
        p.drawRect(x, y - 20, 3, 20);
        // ascender
        if (g % 2 == 0) {
          p.drawRect(x, y - 32, 3, 12);
        }
        // sparse crossbar for rotated signal
        if (g % 17 == 0) {
          p.drawRect(x - 2, y - 12, 8, 3);
        }
      }
    }
  }

  // no mirror

  QImage out;
  if (rotation == 0) {
    out = base;
  } else if (rotation == 180) {
    out = QImage(base.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(180);
    p.translate(-base.width() / 2.0, -base.height() / 2.0);
    p.drawImage(0, 0, base);
  } else {
    out = QImage(base.height(), base.width(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(rotation);
    p.translate(-base.width() / 2.0, -base.height() / 2.0);
    p.drawImage(0, 0, base);
  }

  const Dpm dpm(Dpi(300, 300));
  out.setDotsPerMeterX(dpm.horizontal());
  out.setDotsPerMeterY(dpm.vertical());
  return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OrientationDetectorSuite)

BOOST_AUTO_TEST_CASE(text_page_four_rotations) {
  const int expectedFor[] = {0, 270, 180, 90};
  int idx = 0;
  for (int rot : {0, 90, 180, 270}) {
    QImage page = makeTextPage(rot);
    const auto result = OrientationDetector::detect(page, 300);
    BOOST_TEST(result.decided);
    BOOST_CHECK_EQUAL(result.angle, expectedFor[idx++]);
    // confidence should be clear of threshold
    if (rot == 0 || rot == 180) {
      BOOST_CHECK(std::fabs(result.upConfidence) > 8.0);
    } else {
      BOOST_CHECK(std::fabs(result.leftConfidence) > 8.0);
    }
  }
}

BOOST_AUTO_TEST_CASE(empty_page_undecided) {
  QImage page(800, 1000, QImage::Format_RGB32);
  page.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const auto result = OrientationDetector::detect(page, 300);
  BOOST_CHECK(!result.decided);
}

BOOST_AUTO_TEST_CASE(noise_undecided) {
  QImage page(800, 1000, QImage::Format_RGB32);
  // deterministic pseudo-noise
  for (int y = 0; y < page.height(); ++y) {
    for (int x = 0; x < page.width(); ++x) {
      int v = (x * 37 + y * 97) % 256;
      page.setPixel(x, y, qRgb(v, v, v));
    }
  }
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const auto result = OrientationDetector::detect(page, 300);
  BOOST_CHECK(!result.decided);
}

BOOST_AUTO_TEST_SUITE_END()