// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

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

BOOST_AUTO_TEST_SUITE_END()
