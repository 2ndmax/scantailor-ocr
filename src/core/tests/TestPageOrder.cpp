// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Dpi.h>
#include <FileNameDisambiguator.h>
#include <ImageId.h>
#include <ImageInfo.h>
#include <ImageMetadata.h>
#include <OutputFileNameGenerator.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSequence.h>
#include <ProjectPages.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

namespace Tests {

namespace {
std::vector<PageId::SubPage> subPagesInOrder(const ProjectPages& pages) {
  std::vector<PageId::SubPage> subPages;
  for (const PageInfo& page : pages.toPageSequence(PAGE_VIEW)) {
    subPages.push_back(page.id().subPage());
  }
  return subPages;
}

void writeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(content);
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QByteArray();
  }
  return file.readAll();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PageOrderTestSuite)

BOOST_AUTO_TEST_CASE(halves_follow_the_layout_direction) {
  const ImageInfo image(ImageId("/scan.png"), ImageMetadata(QSize(2000, 1000), Dpi(300, 300)), 2, false, false);
  ProjectPages pages({image}, Qt::LeftToRight);
  BOOST_CHECK(subPagesInOrder(pages) == (std::vector<PageId::SubPage>{PageId::LEFT_PAGE, PageId::RIGHT_PAGE}));

  pages.setLayoutDirection(Qt::RightToLeft);
  BOOST_CHECK(pages.layoutDirection() == Qt::RightToLeft);
  BOOST_CHECK(subPagesInOrder(pages) == (std::vector<PageId::SubPage>{PageId::RIGHT_PAGE, PageId::LEFT_PAGE}));
}

BOOST_AUTO_TEST_CASE(signal_only_on_a_real_change) {
  ProjectPages pages(Qt::LeftToRight);
  std::vector<Qt::LayoutDirection> received;
  int modifiedCount = 0;
  QObject::connect(&pages, &ProjectPages::layoutDirectionChanged,
                   [&](const Qt::LayoutDirection dir) { received.push_back(dir); });
  QObject::connect(&pages, &ProjectPages::modified, [&]() { ++modifiedCount; });

  pages.setLayoutDirection(Qt::LeftToRight);
  BOOST_CHECK(received.empty());
  BOOST_CHECK_EQUAL(modifiedCount, 0);

  pages.setLayoutDirection(Qt::RightToLeft);
  pages.setLayoutDirection(Qt::RightToLeft);
  BOOST_REQUIRE_EQUAL(received.size(), 1u);
  BOOST_CHECK(received[0] == Qt::RightToLeft);
  BOOST_CHECK_EQUAL(modifiedCount, 1);
}

BOOST_AUTO_TEST_CASE(file_names_follow_the_layout_direction) {
  OutputFileNameGenerator generator(std::make_shared<FileNameDisambiguator>(), "/out", Qt::LeftToRight);
  const ImageId imageId("/in/scan.png");
  BOOST_CHECK_EQUAL(generator.fileNameFor(PageId(imageId, PageId::SINGLE_PAGE)).toStdString(), "scan.tif");
  BOOST_CHECK_EQUAL(generator.fileNameFor(PageId(imageId, PageId::LEFT_PAGE)).toStdString(), "scan_1L.tif");
  BOOST_CHECK_EQUAL(generator.fileNameFor(PageId(imageId, PageId::RIGHT_PAGE)).toStdString(), "scan_2R.tif");

  generator.setLayoutDirection(Qt::RightToLeft);
  BOOST_CHECK_EQUAL(generator.fileNameFor(PageId(imageId, PageId::LEFT_PAGE)).toStdString(), "scan_2L.tif");
  BOOST_CHECK_EQUAL(generator.fileNameFor(PageId(imageId, PageId::RIGHT_PAGE)).toStdString(), "scan_1R.tif");
}

BOOST_AUTO_TEST_CASE(switching_renames_the_output_files) {
  QTemporaryDir tmp;
  BOOST_REQUIRE(tmp.isValid());
  const QDir outDir(tmp.path());
  BOOST_REQUIRE(outDir.mkpath("foreground"));
  const QDir foregroundDir(outDir.filePath("foreground"));

  writeFile(outDir.filePath("scan_1L.tif"), "left");
  writeFile(outDir.filePath("scan_2R.tif"), "right");
  writeFile(foregroundDir.filePath("scan_1L.tif"), "left foreground");
  // Left over from earlier, to be replaced.
  writeFile(outDir.filePath("scan_2L.tif"), "stale");
  const QDateTime leftModified = QFileInfo(outDir.filePath("scan_1L.tif")).lastModified();

  OutputFileNameGenerator generator(std::make_shared<FileNameDisambiguator>(), outDir.path(), Qt::LeftToRight);
  const std::vector<ImageId> images{ImageId(outDir.filePath("scan.png"))};
  const QStringList dirs{outDir.path(), foregroundDir.path()};

  generator.setLayoutDirectionRenamingFiles(Qt::RightToLeft, images, dirs);
  BOOST_CHECK(generator.layoutDirection() == Qt::RightToLeft);
  BOOST_CHECK(readFile(outDir.filePath("scan_2L.tif")) == "left");
  BOOST_CHECK(readFile(outDir.filePath("scan_1R.tif")) == "right");
  BOOST_CHECK(readFile(foregroundDir.filePath("scan_2L.tif")) == "left foreground");
  BOOST_CHECK(!QFile::exists(outDir.filePath("scan_1L.tif")));
  BOOST_CHECK(!QFile::exists(outDir.filePath("scan_2R.tif")));
  BOOST_CHECK(!QFile::exists(foregroundDir.filePath("scan_1L.tif")));
  BOOST_CHECK(QFileInfo(outDir.filePath("scan_2L.tif")).lastModified() == leftModified);

  generator.setLayoutDirectionRenamingFiles(Qt::LeftToRight, images, dirs);
  BOOST_CHECK(readFile(outDir.filePath("scan_1L.tif")) == "left");
  BOOST_CHECK(readFile(outDir.filePath("scan_2R.tif")) == "right");
  BOOST_CHECK(readFile(foregroundDir.filePath("scan_1L.tif")) == "left foreground");
  BOOST_CHECK_EQUAL(outDir.entryList(QDir::Files).size(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
