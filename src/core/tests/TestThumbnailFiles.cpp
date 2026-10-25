// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageId.h>
#include <ThumbnailPixmapCache.h>

#include <QDir>
#include <QImage>
#include <QSize>
#include <QStringList>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

namespace Tests {

namespace {
QStringList thumbsStartingWith(const QDir& thumbDir, const QString& prefix) {
  QStringList names;
  for (const QString& name : thumbDir.entryList(QDir::Files)) {
    if (name.startsWith(prefix)) {
      names.push_back(name);
    }
  }
  return names;
}

// Writes the thumbnail of the image in two sizes.
void writeThumbs(ThumbnailPixmapCache& cache, const ImageId& imageId) {
  QImage image(600, 800, QImage::Format_RGB32);
  image.fill(Qt::white);
  cache.setMaxThumbSize(QSize(250, 250));
  cache.recreateThumbnail(imageId, image);
  cache.setMaxThumbSize(QSize(400, 400));
  cache.ensureThumbnailExists(imageId, image);
}

QImage whiteImage() {
  QImage image(600, 800, QImage::Format_RGB32);
  image.fill(Qt::white);
  return image;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ThumbnailFilesTestSuite)

BOOST_AUTO_TEST_CASE(rename_and_remove_cover_all_sizes) {
  QTemporaryDir tmp;
  BOOST_REQUIRE(tmp.isValid());
  const QDir outDir(tmp.path());
  BOOST_REQUIRE(outDir.mkpath("other"));
  const QDir thumbDir(outDir.filePath("thumbs"));
  ThumbnailPixmapCache cache(thumbDir.path(), QSize(250, 250), 40, 5);

  const ImageId left(outDir.filePath("scan_1L.tif"));
  const ImageId renamedLeft(outDir.filePath("scan_2L.tif"));
  // Same name in another folder: its thumbnails differ only by the hash of the path.
  const ImageId otherLeft(outDir.filePath("other/scan_1L.tif"));
  writeThumbs(cache, left);
  writeThumbs(cache, otherLeft);
  // Left over from earlier, to be replaced.
  writeThumbs(cache, renamedLeft);
  BOOST_REQUIRE_EQUAL(thumbsStartingWith(thumbDir, "scan_1L_").size(), 4);
  BOOST_REQUIRE_EQUAL(thumbsStartingWith(thumbDir, "scan_2L_").size(), 2);

  cache.renameThumbnails(left, renamedLeft);
  BOOST_CHECK_EQUAL(thumbsStartingWith(thumbDir, "scan_1L_").size(), 2);
  BOOST_CHECK_EQUAL(thumbsStartingWith(thumbDir, "scan_2L_").size(), 2);
  BOOST_CHECK(thumbsStartingWith(thumbDir, "scan_2L_").filter("_q250.png").size() == 1);
  BOOST_CHECK(thumbsStartingWith(thumbDir, "scan_2L_").filter("_q400.png").size() == 1);

  cache.removeThumbnails(renamedLeft);
  BOOST_CHECK(thumbsStartingWith(thumbDir, "scan_2L_").isEmpty());
  // The thumbnails of the other image stay.
  BOOST_CHECK_EQUAL(thumbsStartingWith(thumbDir, "scan_1L_").size(), 2);
  BOOST_CHECK_EQUAL(thumbDir.entryList(QDir::Files).size(), 2);
}

BOOST_AUTO_TEST_CASE(recreate_removes_other_sizes) {
  // Otherwise switching the thumbnail quality back shows an old processing result.
  QTemporaryDir tmp;
  BOOST_REQUIRE(tmp.isValid());
  const QDir thumbDir(QDir(tmp.path()).filePath("thumbs"));
  ThumbnailPixmapCache cache(thumbDir.path(), QSize(400, 400), 40, 5);
  const ImageId imageId(QDir(tmp.path()).filePath("scan.tif"));

  cache.recreateThumbnail(imageId, whiteImage());
  BOOST_REQUIRE_EQUAL(thumbsStartingWith(thumbDir, "scan_").filter("_q400.png").size(), 1);

  cache.setMaxThumbSize(QSize(250, 250));
  cache.recreateThumbnail(imageId, whiteImage());
  BOOST_CHECK_EQUAL(thumbsStartingWith(thumbDir, "scan_").filter("_q250.png").size(), 1);
  BOOST_CHECK(thumbsStartingWith(thumbDir, "scan_").filter("_q400.png").isEmpty());
}

BOOST_AUTO_TEST_CASE(set_thumb_dir_creates_the_folder) {
  // Relinking to a new output folder creates only <out>/cache, not <out>/cache/thumbs.
  QTemporaryDir tmp;
  BOOST_REQUIRE(tmp.isValid());
  const QDir outDir(tmp.path());
  BOOST_REQUIRE(outDir.mkpath("old/cache"));
  BOOST_REQUIRE(outDir.mkpath("new/cache"));
  ThumbnailPixmapCache cache(outDir.filePath("old/cache/thumbs"), QSize(250, 250), 40, 5);

  const QDir newThumbDir(outDir.filePath("new/cache/thumbs"));
  cache.setThumbDir(newThumbDir.path());
  BOOST_CHECK(newThumbDir.exists());

  cache.recreateThumbnail(ImageId(outDir.filePath("scan.tif")), whiteImage());
  BOOST_CHECK_EQUAL(thumbsStartingWith(newThumbDir, "scan_").size(), 1);
}

BOOST_AUTO_TEST_CASE(rename_to_itself_keeps_thumbnails) {
  QTemporaryDir tmp;
  BOOST_REQUIRE(tmp.isValid());
  const QDir thumbDir(QDir(tmp.path()).filePath("thumbs"));
  ThumbnailPixmapCache cache(thumbDir.path(), QSize(250, 250), 40, 5);
  const ImageId imageId(QDir(tmp.path()).filePath("scan_1L.tif"));
  writeThumbs(cache, imageId);
  BOOST_REQUIRE_EQUAL(thumbsStartingWith(thumbDir, "scan_1L_").size(), 2);

  cache.renameThumbnails(imageId, imageId);
  BOOST_CHECK_EQUAL(thumbsStartingWith(thumbDir, "scan_1L_").size(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
