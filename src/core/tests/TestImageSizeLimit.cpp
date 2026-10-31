// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageMetadata.h>
#include <ImageSizeLimit.h>

#include <QSize>
#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_AUTO_TEST_SUITE(ImageSizeLimitTestSuite)

BOOST_AUTO_TEST_CASE(test_a0_at_600_dpi_is_allowed) {
  BOOST_CHECK(!ImageSizeLimit::exceeds(QSize(19866, 28087)));
}

BOOST_AUTO_TEST_CASE(test_longest_side) {
  BOOST_CHECK(!ImageSizeLimit::exceeds(ImageSizeLimit::MAX_SIDE, 1));
  BOOST_CHECK(!ImageSizeLimit::exceeds(1, ImageSizeLimit::MAX_SIDE));
  BOOST_CHECK(ImageSizeLimit::exceeds(ImageSizeLimit::MAX_SIDE + 1, 1));
  BOOST_CHECK(ImageSizeLimit::exceeds(1, ImageSizeLimit::MAX_SIDE + 1));
}

BOOST_AUTO_TEST_CASE(test_number_of_pixels) {
  // 20000 x 30000 is exactly the limit, one more line is too much.
  BOOST_REQUIRE_EQUAL(ImageSizeLimit::MAX_PIXELS, qint64(20000) * 30000);
  BOOST_CHECK(!ImageSizeLimit::exceeds(20000, 30000));
  BOOST_CHECK(ImageSizeLimit::exceeds(20000, 30001));
  BOOST_CHECK(ImageSizeLimit::exceeds(ImageSizeLimit::MAX_SIDE, ImageSizeLimit::MAX_SIDE));
}

BOOST_AUTO_TEST_CASE(test_sizes_beyond_int_from_image_headers) {
  const qint64 huge = qint64(std::numeric_limits<int>::max()) + 1;
  BOOST_CHECK(ImageSizeLimit::exceeds(huge, 1));
  BOOST_CHECK(ImageSizeLimit::exceeds(std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::max()));
}

BOOST_AUTO_TEST_CASE(test_empty_sizes_are_not_too_large) {
  BOOST_CHECK(!ImageSizeLimit::exceeds(QSize()));
  BOOST_CHECK(!ImageSizeLimit::exceeds(0, 100000));
  BOOST_CHECK(!ImageSizeLimit::exceeds(-1, 100000));
}

BOOST_AUTO_TEST_CASE(test_error_message_names_size_and_limits) {
  const QString message = ImageSizeLimit::errorMessage(100000, 200);
  BOOST_CHECK(message.contains(QStringLiteral("100000")));
  BOOST_CHECK(message.contains(QStringLiteral("200")));
  BOOST_CHECK(message.contains(QString::number(ImageSizeLimit::MAX_SIDE)));
  BOOST_CHECK(message.contains(QStringLiteral("600")));
}

// The steps before Output scale a page to up to 300 DPI. A far too low DPI in the file header
// would make that image exceed the limit, so the DPI must be fixed first, like an undefined one.
BOOST_AUTO_TEST_CASE(test_dpi_too_small_to_process_must_be_fixed) {
  const ImageMetadata wrongDpi(QSize(20000, 28000), Dpi(5, 5));
  BOOST_CHECK(!wrongDpi.isDpiOK());
  BOOST_CHECK_EQUAL(wrongDpi.horizontalDpiStatus(), ImageMetadata::DPI_TOO_SMALL_TO_PROCESS);
  BOOST_CHECK_EQUAL(wrongDpi.verticalDpiStatus(), ImageMetadata::DPI_TOO_SMALL_TO_PROCESS);

  // Only the axis that is scaled up carries the status.
  const ImageMetadata oneAxis(QSize(20000, 20000), Dpi(600, 5));
  BOOST_CHECK(!oneAxis.isDpiOK());
  BOOST_CHECK_EQUAL(oneAxis.verticalDpiStatus(), ImageMetadata::DPI_TOO_SMALL_TO_PROCESS);
  BOOST_CHECK_NE(oneAxis.horizontalDpiStatus(), ImageMetadata::DPI_TOO_SMALL_TO_PROCESS);
}

BOOST_AUTO_TEST_CASE(test_dpi_at_the_edge_of_processing) {
  // At 150 DPI the image doubles: 20000 x 30000 at 300 DPI is exactly the limit, one more line is too much.
  BOOST_CHECK(ImageMetadata(QSize(10000, 15000), Dpi(150, 150)).isDpiOK());
  BOOST_CHECK(!ImageMetadata(QSize(10000, 15001), Dpi(150, 150)).isDpiOK());
  // The longest side: 32767 px at 300 DPI fits.
  BOOST_CHECK(ImageMetadata(QSize(32767, 100), Dpi(300, 300)).isDpiOK());
}

BOOST_AUTO_TEST_CASE(test_low_dpi_that_fits_stays_processable) {
  // A screenshot at 72 DPI is doubtful (too small), but it can be processed.
  const ImageMetadata screenshot(QSize(1920, 1080), Dpi(72, 72));
  BOOST_CHECK(screenshot.isDpiOK());
  BOOST_CHECK_EQUAL(screenshot.horizontalDpiStatus(), ImageMetadata::DPI_TOO_SMALL);
}

BOOST_AUTO_TEST_CASE(test_undefined_dpi_on_one_axis) {
  const ImageMetadata metadata(QSize(20000, 28000), Dpi(0, 5));
  BOOST_CHECK(!metadata.isDpiOK());
  BOOST_CHECK_EQUAL(metadata.horizontalDpiStatus(), ImageMetadata::DPI_UNDEFINED);
}

BOOST_AUTO_TEST_SUITE_END()
