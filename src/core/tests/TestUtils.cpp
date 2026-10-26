// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <core/Utils.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "TestHelpers.h"

BOOST_AUTO_TEST_SUITE(CoreUtilsTestSuite)

BOOST_AUTO_TEST_CASE(test_overwriting_rename) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString source = dir.filePath("source.txt");
  const QString target = dir.filePath("target.txt");
  BOOST_REQUIRE(Tests::writeFile(source, "new"));
  BOOST_REQUIRE(Tests::writeFile(target, "old"));

  BOOST_REQUIRE(core::Utils::overwritingRename(source, target));
  BOOST_CHECK(!QFile::exists(source));
  BOOST_CHECK(Tests::readFile(target) == "new");
}

BOOST_AUTO_TEST_CASE(test_overwriting_rename_long_path) {
  // Paths longer than MAX_PATH (260 characters), such as thumbnails of pages whose
  // names grew with each scan inserted after them. Windows refused to replace them.
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString longDir = dir.filePath(QString(200, QChar('d')));
  BOOST_REQUIRE(QDir().mkpath(longDir));
  const QString source = longDir + QStringLiteral("/source.txt");
  const QString target = longDir + '/' + QString(80, QChar('t')) + QStringLiteral(".txt");
  BOOST_REQUIRE(target.size() > 260);
  BOOST_REQUIRE(Tests::writeFile(source, "new"));
  BOOST_REQUIRE(Tests::writeFile(target, "old"));

  BOOST_REQUIRE(core::Utils::overwritingRename(source, target));
  BOOST_CHECK(!QFile::exists(source));
  BOOST_CHECK(Tests::readFile(target) == "new");

  // Also when the target doesn't exist yet.
  BOOST_REQUIRE(Tests::writeFile(source, "newer"));
  const QString other = longDir + '/' + QString(80, QChar('o')) + QStringLiteral(".txt");
  BOOST_REQUIRE(core::Utils::overwritingRename(source, other));
  BOOST_CHECK(Tests::readFile(other) == "newer");
}

BOOST_AUTO_TEST_SUITE_END()
