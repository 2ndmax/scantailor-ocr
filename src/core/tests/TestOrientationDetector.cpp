// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <OrientationDetector.h>

#include <Dpi.h>
#include <Dpm.h>
#include <QImage>
#include <QPainter>
#include <QTransform>
#include <boost/test/unit_test.hpp>
#include <cmath>

#include <leptonica/allheaders.h>

#include "LeptonicaPix.h"
#include "TestHelpers.h"
#include "imageproc/BinaryThreshold.h"
#include "imageproc/GrayImage.h"
#include "imageproc/Scale.h"

namespace {
// Leptonica's makeOrientDecision equivalent, recomputed here for comparison.
int leptAngleFor(float up, float left) {
  const l_float32 minupconf = 8.0f;
  const l_float32 minratio = 2.5f;
  const l_float32 absup = std::fabs(up);
  const l_float32 absleft = std::fabs(left);

  l_int32 orient = L_TEXT_ORIENT_UNKNOWN;
  if (up != 0.0f && left != 0.0f) {
    if (up > minupconf && absup > minratio * absleft) {
      orient = L_TEXT_ORIENT_UP;
    } else if (left > minupconf && absleft > minratio * absup) {
      orient = L_TEXT_ORIENT_LEFT;
    } else if (up < -minupconf && absup > minratio * absleft) {
      orient = L_TEXT_ORIENT_DOWN;
    } else if (left < -minupconf && absleft > minratio * absup) {
      orient = L_TEXT_ORIENT_RIGHT;
    }
  }
  switch (orient) {
    case L_TEXT_ORIENT_UP:
      return 0;
    case L_TEXT_ORIENT_LEFT:
      return 90;
    case L_TEXT_ORIENT_DOWN:
      return 180;
    case L_TEXT_ORIENT_RIGHT:
      return 270;
    default:
      return -1;
  }
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OrientationDetectorSuite)

BOOST_AUTO_TEST_CASE(text_page_four_rotations) {
  const int expectedFor[] = {0, 270, 180, 90};
  int idx = 0;
  for (int rot : {0, 90, 180, 270}) {
    QImage page = Tests::makeTextPage(rot, 300);
    const auto result = OrientationDetector::detect(page, 300);
    BOOST_TEST(result.decided);
    BOOST_CHECK_EQUAL(result.angle, expectedFor[idx++]);
    if (rot == 0 || rot == 180) {
      BOOST_CHECK(std::fabs(result.upConfidence) > 8.0);
    } else {
      BOOST_CHECK(std::fabs(result.leftConfidence) > 8.0);
    }
  }
}

BOOST_AUTO_TEST_CASE(text_page_four_rotations_600dpi) {
  const int expectedFor[] = {0, 270, 180, 90};
  int idx = 0;
  for (int rot : {0, 90, 180, 270}) {
    QImage page = Tests::makeTextPage(rot, 600);
    const auto result = OrientationDetector::detect(page, 600);
    BOOST_TEST(result.decided);
    BOOST_CHECK_EQUAL(result.angle, expectedFor[idx++]);
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

BOOST_AUTO_TEST_CASE(pseudo_random_noise_undecided) {
  // A simple linear congruential generator, fixed seed: independent black/white
  // noise with no text-like structure, so the detector must stay undecided.
  QImage page(800, 1000, QImage::Format_RGB32);
  unsigned int state = 12345u;
  for (int y = 0; y < page.height(); ++y) {
    for (int x = 0; x < page.width(); ++x) {
      state = state * 1103515245u + 12345u;
      const int v = (state >> 16) & 0xff;
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
  // Table of (up, left) confidence pairs hitting every branch of the decision.
  struct Row {
    float up;
    float left;
  };
  const Row rows[] = {
      {0.0f, 0.0f},   // both zero -> undecided
      {10.0f, 0.0f},  // one zero -> undecided
      {0.0f, 10.0f},  // one zero -> undecided
      {7.9f, 1.0f},   // just below minupconf -> undecided
      {8.1f, 1.0f},   // just above minupconf -> up
      {-7.9f, 1.0f},  // just below (negative) -> undecided
      {-8.1f, 1.0f},  // just above (negative) -> down
      {10.0f, 3.9f},  // ratio just above 2.5 -> up
      {10.0f, 4.1f},  // ratio just below 2.5 -> undecided
      {1.0f, 8.1f},   // left decided -> 90
      {1.0f, -8.1f},  // right decided -> 270
      {-1.0f, 8.1f},  // left still wins -> 90
  };

  for (const Row& r : rows) {
    const l_int32 oldSeverity = setMsgSeverity(L_SEVERITY_WARNING);
    l_int32 orient = L_TEXT_ORIENT_UNKNOWN;
    makeOrientDecision(r.up, r.left, 0.0f, 0.0f, &orient, 0);
    setMsgSeverity(oldSeverity);

    int leptAngle = -1;
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

    const auto result = OrientationDetector::decide(r.up, r.left);
    BOOST_CHECK_EQUAL(result.decided, leptDecided);
    if (leptDecided) {
      BOOST_CHECK_EQUAL(result.angle, leptAngle);
    } else {
      BOOST_CHECK(!result.decided);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()