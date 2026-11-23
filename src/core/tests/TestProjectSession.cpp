// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <PageId.h>
#include <PageSequence.h>
#include <PageView.h>
#include <ProcessingOptions.h>
#include <ProjectFiles.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectSession.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTemporaryDir>
#include <map>
#include <set>
#include <utility>

#include <boost/test/unit_test.hpp>

#include "TestHelpers.h"

namespace {
QString createProject(const QTemporaryDir& dir) {
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath("page.png"), 300)};
  BOOST_REQUIRE(!files.front().imageInfo().empty());

  const QString outDir = dir.filePath("out");
  BOOST_REQUIRE(QDir().mkpath(outDir));

  const DefaultParams startValues;
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);

  const QString projectFile = dir.filePath("project.ScanTailor");
  BOOST_REQUIRE(session.write(projectFile));
  return projectFile;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectSessionTestSuite)

BOOST_AUTO_TEST_CASE(loading_without_side_effects_leaves_deleted_output_folder_absent) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  const QString projectFile = createProject(dir);
  const QString outDir = dir.filePath("out");

  QDir(outDir).removeRecursively();
  BOOST_REQUIRE(!QDir(outDir).exists());

  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.error == ProjectReader::FileError::NONE);
  BOOST_REQUIRE(read.reader);

  const ProjectSession withoutSideEffects
      = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::WithoutSideEffects);
  BOOST_CHECK(!QDir(outDir).exists());
  BOOST_CHECK(!withoutSideEffects.thumbnailCache);

  const ProjectSession normal = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::Normal);
  BOOST_CHECK(QDir(outDir).exists());
  BOOST_CHECK(normal.thumbnailCache);
  BOOST_CHECK(QDir(outDir + QStringLiteral("/cache")).exists());
}

BOOST_AUTO_TEST_CASE(loading_without_side_effects_does_not_create_cache) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  const QString projectFile = createProject(dir);
  const QString outDir = dir.filePath("out");
  BOOST_REQUIRE(QDir(outDir).exists());
  QDir(outDir + QStringLiteral("/cache")).removeRecursively();
  BOOST_REQUIRE(!QDir(outDir + QStringLiteral("/cache")).exists());

  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.error == ProjectReader::FileError::NONE);
  BOOST_REQUIRE(read.reader);

  const ProjectSession withoutSideEffects
      = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::WithoutSideEffects);
  BOOST_CHECK(!QDir(outDir + QStringLiteral("/cache")).exists());
  BOOST_CHECK(!withoutSideEffects.thumbnailCache);

  const ProjectSession normal = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::Normal);
  BOOST_CHECK(QDir(outDir + QStringLiteral("/cache")).exists());
  BOOST_CHECK(normal.thumbnailCache);
}

BOOST_AUTO_TEST_CASE(loading_both_modes_has_the_same_content) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  const QString projectFile = createProject(dir);
  const QString outDir = dir.filePath("out");

  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.error == ProjectReader::FileError::NONE);
  BOOST_REQUIRE(read.reader);

  const ProjectSession withoutSideEffects
      = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::WithoutSideEffects);
  const ProjectSession normal = ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::Normal);

  const PageSequence withoutPages = withoutSideEffects.pages->toPageSequence(PAGE_VIEW);
  const PageSequence normalPages = normal.pages->toPageSequence(PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(withoutPages.numPages(), normalPages.numPages());

  for (size_t i = 0; i < withoutPages.numPages(); ++i) {
    BOOST_CHECK(withoutPages.pageAt(i).imageId() == normalPages.pageAt(i).imageId());
    BOOST_CHECK(withoutSideEffects.outFileNameGen.filePathFor(withoutPages.pageAt(i).id())
                == normal.outFileNameGen.filePathFor(normalPages.pageAt(i).id()));
  }

  BOOST_CHECK(withoutSideEffects.selectedPage.get(PAGE_VIEW) == normal.selectedPage.get(PAGE_VIEW));
  BOOST_CHECK(withoutSideEffects.outFileNameGen.outDir() == normal.outFileNameGen.outDir());
}

BOOST_AUTO_TEST_CASE(loading_without_side_effects_leaves_folder_unchanged) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  const QString projectFile = createProject(dir);

  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.error == ProjectReader::FileError::NONE);
  BOOST_REQUIRE(read.reader);

  const QByteArray beforeProject = Tests::readFile(projectFile);
  const auto before = Tests::snapshotDirectory(dir.path());

  ProjectSession::load(*read.reader, DefaultParams(), ProjectSession::LoadMode::WithoutSideEffects);

  const QByteArray afterProject = Tests::readFile(projectFile);
  const auto after = Tests::snapshotDirectory(dir.path());

  BOOST_CHECK(beforeProject == afterProject);

  std::set<QString> beforePaths;
  std::set<QString> afterPaths;
  for (const auto& entry : before) {
    beforePaths.insert(entry.first);
  }
  for (const auto& entry : after) {
    afterPaths.insert(entry.first);
  }
  BOOST_CHECK(beforePaths == afterPaths);

  for (const QString& path : beforePaths) {
    if (!before.at(path).second) {
      BOOST_CHECK(before.at(path).first == after.at(path).first);
    }
  }
}

BOOST_AUTO_TEST_SUITE_END()