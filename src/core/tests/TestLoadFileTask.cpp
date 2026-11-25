// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BackgroundTask.h>
#include <BlackOnWhiteEstimator.h>
#include <LoadFileTask.h>
#include <imageproc/BinaryImage.h>
#include <imageproc/GrayImage.h>

#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include <string>

#include "filters/output/OutputGenerator.h"

BOOST_AUTO_TEST_SUITE(LoadFileTaskTestSuite)

BOOST_AUTO_TEST_CASE(test_unexpected_error_reason) {
  BOOST_CHECK_EQUAL(LoadFileTask::unexpectedErrorReason(QStringLiteral("sample failure")).toStdString(),
                    "Internal processing error (technical detail: sample failure).");
  BOOST_CHECK_EQUAL(LoadFileTask::unexpectedErrorReason(QStringLiteral("   ")).toStdString(),
                    "Internal processing error.");
}

BOOST_AUTO_TEST_CASE(test_internal_exception_messages_are_clean) {
  bool threwEstimator = false;
  try {
    const imageproc::GrayImage gray(QSize(4, 4));
    const imageproc::BinaryImage mask(QSize(5, 5));
    (void) BlackOnWhiteEstimator::isBlackOnWhite(gray, mask);
  } catch (const std::invalid_argument& e) {
    threwEstimator = true;
    BOOST_CHECK_EQUAL(std::string(e.what()), "BlackOnWhiteEstimator: image and mask have different sizes.");
  }
  BOOST_CHECK(threwEstimator);

  bool threwMargins = false;
  try {
    QImage img(4, 4, QImage::Format_Indexed8);
    const imageproc::BinaryImage mask(QSize(5, 5));
    output::fillMarginsInPlace(img, mask, Qt::white);
  } catch (const std::invalid_argument& e) {
    threwMargins = true;
    BOOST_CHECK_EQUAL(std::string(e.what()), "fillMarginsInPlace: image and mask have different sizes.");
  }
  BOOST_CHECK(threwMargins);

  const BackgroundTask::CancelledException cancelled;
  BOOST_CHECK_EQUAL(std::string(cancelled.what()), "BackgroundTask canceled.");
}

BOOST_AUTO_TEST_SUITE_END()
