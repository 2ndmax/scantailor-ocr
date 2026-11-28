// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AutoRemovingFile.h>

#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <type_traits>
#include <utility>

static_assert(!std::is_copy_constructible_v<AutoRemovingFile>);
static_assert(!std::is_copy_assignable_v<AutoRemovingFile>);
static_assert(std::is_nothrow_move_constructible_v<AutoRemovingFile>);
static_assert(std::is_nothrow_move_assignable_v<AutoRemovingFile>);

namespace {
QString createFile(const QTemporaryDir& dir, const QString& name) {
  const QString path(dir.filePath(name));
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::WriteOnly));
  file.write("x");
  return path;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(FoundationAutoRemovingFileTestSuite)

BOOST_AUTO_TEST_CASE(test_assignment_removes_the_old_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString pathA(createFile(dir, "a"));
  const QString pathB(createFile(dir, "b"));
  {
    AutoRemovingFile fileA(pathA);
    AutoRemovingFile fileB(pathB);
    fileA = std::move(fileB);
    BOOST_CHECK(!QFile::exists(pathA));
    BOOST_CHECK(QFile::exists(pathB));
    BOOST_CHECK(fileA.get() == pathB);
    BOOST_CHECK(fileB.get().isNull());
  }
  BOOST_CHECK(!QFile::exists(pathB));
}

BOOST_AUTO_TEST_CASE(test_move_construction_transfers_ownership) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path(createFile(dir, "moved"));
  {
    AutoRemovingFile fileA(path);
    AutoRemovingFile fileB(std::move(fileA));
    BOOST_CHECK(fileA.get().isNull());
    BOOST_CHECK(fileB.get() == path);
    BOOST_CHECK(QFile::exists(path));
  }
  BOOST_CHECK(!QFile::exists(path));
}

BOOST_AUTO_TEST_CASE(test_self_assignment_keeps_the_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path(createFile(dir, "a"));
  AutoRemovingFile file(path);
  AutoRemovingFile& alias = file;
  file = std::move(alias);
  BOOST_CHECK(QFile::exists(path));
  BOOST_CHECK(file.get() == path);
}

BOOST_AUTO_TEST_SUITE_END()
