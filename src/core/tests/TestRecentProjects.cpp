// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <RecentProjects.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
std::vector<QString> listed(const RecentProjects& recent) {
  std::vector<QString> result;
  recent.enumerate([&](const QString& path) { result.push_back(path); });
  return result;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(RecentProjectsTestSuite)

// The same project file reached by another spelling of its path (separators, "..") is one entry.
BOOST_AUTO_TEST_CASE(test_same_file_in_other_spelling_is_listed_once) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QDir root(dir.path());
  BOOST_REQUIRE(root.mkdir("sub"));
  const QString first = root.absoluteFilePath("a.ScanTailor");
  const QString second = root.absoluteFilePath("b.ScanTailor");

  RecentProjects recent;
  recent.setMostRecent(first);
  recent.setMostRecent(second);
  recent.setMostRecent(QDir::toNativeSeparators(first));
  recent.setMostRecent(root.absoluteFilePath("sub/../a.ScanTailor"));

  const std::vector<QString> entries = listed(recent);
  BOOST_REQUIRE_EQUAL(entries.size(), 2u);
  BOOST_CHECK(QDir::cleanPath(entries[0]) == QDir::cleanPath(first));
  BOOST_CHECK(QDir::cleanPath(entries[1]) == QDir::cleanPath(second));
}

#ifdef Q_OS_WIN
BOOST_AUTO_TEST_CASE(test_letter_case_is_ignored_on_windows) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = QDir(dir.path()).absoluteFilePath("Projekt.ScanTailor");

  RecentProjects recent;
  recent.setMostRecent(path);
  recent.setMostRecent(path.toUpper());
  BOOST_CHECK_EQUAL(listed(recent).size(), 1u);
}
#endif

BOOST_AUTO_TEST_SUITE_END()
