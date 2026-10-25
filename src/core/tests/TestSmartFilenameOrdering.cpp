// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <SmartFilenameOrdering.h>

#include <QFileInfo>
#include <QString>
#include <boost/test/unit_test.hpp>
#include <utility>

namespace Tests {
BOOST_AUTO_TEST_SUITE(SmartFilenameOrderingTestSuite)

BOOST_AUTO_TEST_CASE(test_same_file) {
  const SmartFilenameOrdering less;
  const QFileInfo somefile("/etc/somefile");
  BOOST_CHECK(!less(somefile, somefile));
}

BOOST_AUTO_TEST_CASE(test_dirs_different) {
  const SmartFilenameOrdering less;
  // Files in different folders are ordered by folder: "/ect" before "/etc".
  const QFileInfo lhs("/etc/file");
  const QFileInfo rhs("/ect/file");
  BOOST_CHECK(!less(lhs, rhs));
  BOOST_CHECK(less(rhs, lhs));
}

BOOST_AUTO_TEST_CASE(test_simple_case) {
  const SmartFilenameOrdering less;
  const QFileInfo lhs("/etc/1.png");
  const QFileInfo rhs("/etc/2.png");
  BOOST_CHECK(less(lhs, rhs));
  BOOST_CHECK(!less(rhs, lhs));
}

BOOST_AUTO_TEST_CASE(test_avg_case) {
  const SmartFilenameOrdering less;
  const QFileInfo lhs("/etc/a_0002.png");
  const QFileInfo rhs("/etc/a_1.png");
  BOOST_CHECK(!less(lhs, rhs));
  BOOST_CHECK(less(rhs, lhs));
}

BOOST_AUTO_TEST_CASE(test_complex_case) {
  const SmartFilenameOrdering less;
  const QFileInfo lhs("/etc/a10_10.png");
  const QFileInfo rhs("/etc/a010_2.png");
  BOOST_CHECK(!less(lhs, rhs));
  BOOST_CHECK(less(rhs, lhs));
}

BOOST_AUTO_TEST_CASE(test_almost_equal) {
  const SmartFilenameOrdering less;
  const QFileInfo lhs("/etc/10.png");
  const QFileInfo rhs("/etc/010.png");
  BOOST_CHECK(!less(lhs, rhs));
  BOOST_CHECK(less(rhs, lhs));
}

// Numbers too long for 32 or 64 bits, as in file names with time stamps.
BOOST_AUTO_TEST_CASE(test_long_numbers) {
  const SmartFilenameOrdering less;
  const std::pair<QString, QString> ordered[] = {
      {"/etc/4294967295.png", "/etc/4294967296.png"},
      {"/etc/scan_20260929092518.png", "/etc/scan_20260929092519.png"},
      {"/etc/99999999999999999999.png", "/etc/100000000000000000000.png"},
      {"/etc/123456789012345678901_2.png", "/etc/123456789012345678901_10.png"},
  };
  for (const auto& [lhs, rhs] : ordered) {
    BOOST_TEST_CONTEXT(lhs.toStdString() << " < " << rhs.toStdString()) {
      BOOST_CHECK(less(QFileInfo(lhs), QFileInfo(rhs)));
      BOOST_CHECK(!less(QFileInfo(rhs), QFileInfo(lhs)));
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests