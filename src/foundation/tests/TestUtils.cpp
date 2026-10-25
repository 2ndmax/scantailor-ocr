// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Utils.h>

#include <QLocale>
#include <QString>
#include <boost/test/unit_test.hpp>
#include <vector>

using namespace foundation;

BOOST_AUTO_TEST_SUITE(FoundationUtilsTestSuite)

BOOST_AUTO_TEST_CASE(test_double_to_string_zero) {
  BOOST_CHECK_EQUAL(Utils::doubleToString(0.0).toStdString(), "0");
}

BOOST_AUTO_TEST_CASE(test_double_to_string_positive) {
  const QString s = Utils::doubleToString(123.456);
  BOOST_CHECK(s.contains("123"));
  BOOST_CHECK(s.contains("456") || s.contains("46"));
}

BOOST_AUTO_TEST_CASE(test_double_to_string_negative) {
  const QString s = Utils::doubleToString(-42.5);
  BOOST_CHECK(s.startsWith("-") || s.contains("-"));
  BOOST_CHECK(s.contains("42"));
}

// Project files must read the same everywhere, also under a locale with a decimal comma.
BOOST_AUTO_TEST_CASE(test_double_to_string_locale_independent) {
  const QLocale previous;
  QLocale::setDefault(QLocale(QLocale::German));
  const QString s = Utils::doubleToString(1.5);
  QLocale::setDefault(previous);
  BOOST_CHECK_EQUAL(s.toStdString(), "1.5");
}

// A value read back from a project file has to equal the one written, or the page counts as
// changed after reopening. 0.1 * 12 is 1.2000000000000002; with 16 digits it became 1.2.
BOOST_AUTO_TEST_CASE(test_double_to_string_round_trip) {
  std::vector<double> values = {0.1, 1.0 / 3.0, 1e-300, 1e300, -42.5, 2.0 / 3.0 * 1e10};
  for (int slider = 0; slider <= 100; ++slider) {
    values.push_back(0.1 * slider);
  }
  for (const double value : values) {
    const QString s = Utils::doubleToString(value);
    bool ok = false;
    BOOST_CHECK_MESSAGE((s.toDouble(&ok) == value) && ok, s.toStdString());
  }
}

BOOST_AUTO_TEST_SUITE_END()
