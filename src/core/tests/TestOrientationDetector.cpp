#include <OrientationDetector.h>

#include <Dpi.h>
#include <Dpm.h>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <boost/test/unit_test.hpp>
#include <cmath>

#include <leptonica/allheaders.h>

#include "TestHelpers.h"

#include "LeptonicaPix.h"
#include "imageproc/BinaryThreshold.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Scale.h"

BOOST_AUTO_TEST_SUITE(OrientationDetectorSuite)

BOOST_AUTO_TEST_CASE(text_page_four_rotations) {
  const int expectedFor[] = {0, 270, 180, 90};
  int idx = 0;
  for (int rot : {0, 90, 180, 270}) {
    QImage page = Tests::makeTextPage(rot, 300);
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

BOOST_AUTO_TEST_CASE(leptonica_decision_agrees) {
  QImage page = Tests::makeTextPage(0, 300);
  // preprocess like OrientationDetector
  imageproc::GrayImage gray(page);
  int dpi = 300;
  if (dpi > 330) {
    const double f = 300.0 / dpi;
    gray = imageproc::scaleToGray(gray, QSize(qRound(page.width() * f), qRound(page.height() * f)));
  }
  const int threshold = imageproc::BinaryThreshold::otsuThreshold(gray.toQImage());
  const int w = gray.width();
  const int h = gray.height();
  PixPtr pix(pixCreate(w, h, 1));
  l_uint32* data = pixGetData(pix.get());
  const int wpl = pixGetWpl(pix.get());
  for (int y = 0; y < h; ++y) {
    const uchar* line = gray.toQImage().constScanLine(y);
    l_uint32* row = data + y * wpl;
    for (int x = 0; x < w; ++x) {
      if (line[x] < threshold) {
        SET_DATA_BIT(row, x);
      }
    }
  }
  l_float32 up = 0.0f, left = 0.0f;
  pixOrientDetect(pix.get(), &up, &left, 0, 0);

  setMsgSeverity(L_SEVERITY_WARNING);
  l_int32 orient = L_TEXT_ORIENT_UNKNOWN;
  makeOrientDecision(up, left, 0.0f, 0.0f, &orient, 0);
  setMsgSeverity(L_SEVERITY_INFO);

  int leptAngle = 0;
  bool leptDecided = true;
  switch (orient) {
    case L_TEXT_ORIENT_UP:
      leptAngle = 0;
      break;
    case L_TEXT_ORIENT_LEFT:
      leptAngle = 90;
      break;
    case L_TEXT_ORIENT_DOWN:
      leptAngle = 180;
      break;
    case L_TEXT_ORIENT_RIGHT:
      leptAngle = 270;
      break;
    default:
      leptDecided = false;
      break;
  }

  const auto result = OrientationDetector::detect(page, 300);
  BOOST_CHECK_EQUAL(result.decided, leptDecided);
  if (leptDecided) {
    BOOST_CHECK_EQUAL(result.angle, leptAngle);
  }
}

BOOST_AUTO_TEST_SUITE_END()