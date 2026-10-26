// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ProjectFiles.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

namespace {
bool createFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectFilesTestSuite)

BOOST_AUTO_TEST_CASE(test_extension_is_added_once) {
  BOOST_CHECK(project_files::withProjectExtension("C:/Scans/Buch") == "C:/Scans/Buch.ScanTailor");
  BOOST_CHECK(project_files::withProjectExtension("C:/Scans/Buch.ScanTailor") == "C:/Scans/Buch.ScanTailor");
  BOOST_CHECK(project_files::withProjectExtension("C:/Scans/Buch.scantailor") == "C:/Scans/Buch.scantailor");
  BOOST_CHECK(project_files::withProjectExtension("C:/Scans/Buch.txt") == "C:/Scans/Buch.txt.ScanTailor");
  BOOST_CHECK(project_files::withProjectExtension(QString()).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_new_project_is_named_like_its_folder) {
  BOOST_CHECK(project_files::defaultProjectFile("C:/Scans/Buch/") == "C:/Scans/Buch/Buch.ScanTailor");
  BOOST_CHECK(project_files::defaultOutputDir("C:/Scans/Buch/") == "C:/Scans/Buch/out");
}

#ifdef Q_OS_WIN
// A drive root has no folder name.
BOOST_AUTO_TEST_CASE(test_project_in_drive_root_gets_a_name) {
  BOOST_CHECK(project_files::defaultProjectFile("D:/") == "D:/project.ScanTailor");
}
#endif

BOOST_AUTO_TEST_CASE(test_only_images_directly_in_the_folder_are_listed) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QDir root(dir.path());
  BOOST_REQUIRE(root.mkdir("sub"));
  BOOST_REQUIRE(root.mkdir("folder.png"));
  for (const char* name : {"a.png", "b.TIF", "c.jp2", "d.txt", "e.ScanTailor", "sub/f.png"}) {
    BOOST_REQUIRE(createFile(root.absoluteFilePath(name)));
  }

  QStringList names;
  for (const QFileInfo& file : project_files::importableImagesIn(dir.path())) {
    names.push_back(file.fileName());
  }
  names.sort(Qt::CaseInsensitive);
  BOOST_CHECK(names == QStringList({"a.png", "b.TIF", "c.jp2"}));
}

BOOST_AUTO_TEST_SUITE_END()
