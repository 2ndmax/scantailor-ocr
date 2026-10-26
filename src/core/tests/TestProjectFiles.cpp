// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageInfo.h>
#include <ProjectFiles.h>
#include <ProjectPages.h>

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

BOOST_AUTO_TEST_CASE(test_missing_image_means_project_was_moved) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QDir root(dir.path());
  const QString present = root.absoluteFilePath("present.png");
  BOOST_REQUIRE(createFile(present));
  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  const QString outDir = root.absoluteFilePath("out");

  // Both halves of a split image are one file, checked once.
  const ProjectPages inPlace({ImageInfo(ImageId(present), metadata, 2, false, false)}, Qt::LeftToRight);
  BOOST_CHECK(project_files::allImagesExist(inPlace));
  BOOST_CHECK(project_files::isProjectInPlace(inPlace, outDir, QString()));
  // The folder the output folder belongs into is gone.
  BOOST_CHECK(!project_files::isProjectInPlace(inPlace, root.absoluteFilePath("gone/out"), QString()));

  const ProjectPages moved({ImageInfo(ImageId(present), metadata, 1, false, false),
                            ImageInfo(ImageId(root.absoluteFilePath("missing.png")), metadata, 1, false, false)},
                           Qt::LeftToRight);
  BOOST_CHECK(!project_files::allImagesExist(moved));
  BOOST_CHECK(!project_files::isProjectInPlace(moved, outDir, QString()));
}

// Without images only the input folder tells whether the project is where it was.
BOOST_AUTO_TEST_CASE(test_empty_project_is_in_place_with_its_input_folder) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QDir root(dir.path());
  const QString outDir = root.absoluteFilePath("out");
  const ProjectPages empty;

  BOOST_CHECK(project_files::isProjectInPlace(empty, outDir, dir.path()));
  BOOST_CHECK(!project_files::isProjectInPlace(empty, outDir, QString()));
  BOOST_CHECK(!project_files::isProjectInPlace(empty, outDir, root.absoluteFilePath("gone")));
}

BOOST_AUTO_TEST_CASE(test_backup_does_not_overwrite_an_earlier_one) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QDir root(dir.path());
  const QString project = root.absoluteFilePath("Buch.ScanTailor");

  BOOST_CHECK(project_files::backupFilePath(project) == root.absoluteFilePath("Buch_BACKUP.ScanTailor"));
  BOOST_REQUIRE(createFile(root.absoluteFilePath("Buch_BACKUP.ScanTailor")));
  BOOST_CHECK(project_files::backupFilePath(project) == root.absoluteFilePath("Buch_BACKUP_2.ScanTailor"));
  BOOST_REQUIRE(createFile(root.absoluteFilePath("Buch_BACKUP_2.ScanTailor")));
  BOOST_CHECK(project_files::backupFilePath(project) == root.absoluteFilePath("Buch_BACKUP_3.ScanTailor"));
}

BOOST_AUTO_TEST_SUITE_END()
