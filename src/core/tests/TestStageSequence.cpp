// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>

#include <QDir>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#include "TestHelpers.h"

BOOST_AUTO_TEST_SUITE(StageSequenceTestSuite)

// core_tests has no QApplication, only a QCoreApplication, like a command line tool without a
// display: creating a widget would end the program. The steps create their options panels only
// when the GUI asks for them, so a project can be set up and processed without one.
BOOST_AUTO_TEST_CASE(test_process_all_without_gui) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath("page.png"), 300)};
  BOOST_REQUIRE(!files.front().imageInfo().empty());

  const QString outDir = QDir(dir.path()).absoluteFilePath("out");
  BOOST_REQUIRE(QDir().mkpath(outDir));
  const DefaultParams startValues;
  const ProjectSession session = ProjectSession::createNew(ProjectPages::createNew(files, startValues), outDir,
                                                           QString(), startValues, PageSelectionAccessor(nullptr));

  // Without thumbnails: they are pixmaps, which also need a GUI.
  const StageSequence::ProcessAllResult result = session.stages->processAll(nullptr, session.outFileNameGen);
  BOOST_CHECK(result.failures.empty());
  BOOST_CHECK(result.output);
  BOOST_CHECK_EQUAL(QDir(outDir).entryList({"*.tif"}, QDir::Files).size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
