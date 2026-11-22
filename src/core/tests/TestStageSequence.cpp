// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <boost/test/unit_test.hpp>
#include <map>
#include <vector>

#include "ScopedMaxThreads.h"
#include "TestHelpers.h"

BOOST_AUTO_TEST_SUITE(StageSequenceTestSuite)

namespace {
/** A new project of \p numPages scanned pages, page0.png and so on, in \p dir, its output in "out" there. */
ProjectSession createProject(const QTemporaryDir& dir, const int numPages) {
  std::vector<ImageFileInfo> files;
  for (int i = 0; i < numPages; ++i) {
    files.push_back(Tests::saveTextPageScan(dir.filePath(QStringLiteral("page%1.png").arg(i)), 300));
    BOOST_REQUIRE(!files.back().imageInfo().empty());
  }
  const QString outDir = dir.filePath(QStringLiteral("out"));
  BOOST_REQUIRE(QDir().mkpath(outDir));
  const DefaultParams startValues;
  return ProjectSession::createNew(ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()),
                                   outDir, QString(), startValues);
}

/** The output files of \p session by name, with their content. */
std::map<QString, QByteArray> outputFiles(const ProjectSession& session) {
  std::map<QString, QByteArray> files;
  const QDir outDir(session.outFileNameGen.outDir());
  for (const QString& name : outDir.entryList({QStringLiteral("*.tif")}, QDir::Files)) {
    QFile file(outDir.filePath(name));
    BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
    files[name] = file.readAll();
  }
  return files;
}
}  // namespace

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
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);

  // Without thumbnails: they are pixmaps, which also need a GUI.
  const StageSequence::ProcessAllResult result = session.stages->processAll(nullptr, session.outFileNameGen);
  BOOST_CHECK(result.failures.empty());
  BOOST_CHECK(result.output);
  BOOST_CHECK_EQUAL(QDir(outDir).entryList({"*.tif"}, QDir::Files).size(), 1);
}


// Pages processed side by side give the same files as one after the other. Two projects: run
// again, the same project would keep the files that are up to date.
BOOST_AUTO_TEST_CASE(test_parallel_same_output) {
  QTemporaryDir serialDir;
  QTemporaryDir parallelDir;
  BOOST_REQUIRE(serialDir.isValid() && parallelDir.isValid());
  const ProjectSession serial = createProject(serialDir, 4);
  const ProjectSession parallel = createProject(parallelDir, 4);
  {
    const Tests::ScopedMaxThreads threads(1);
    const StageSequence::ProcessAllResult result = serial.stages->processAll(nullptr, serial.outFileNameGen);
    BOOST_CHECK(result.failures.empty());
    BOOST_CHECK(result.output);
  }
  {
    const Tests::ScopedMaxThreads threads(4);
    const StageSequence::ProcessAllResult result = parallel.stages->processAll(nullptr, parallel.outFileNameGen);
    BOOST_CHECK(result.failures.empty());
    BOOST_CHECK(result.output);
  }
  const std::map<QString, QByteArray> serialFiles = outputFiles(serial);
  BOOST_CHECK_EQUAL(serialFiles.size(), 4u);
  BOOST_CHECK(serialFiles == outputFiles(parallel));
}

// The progress is told in the calling thread, one page after the other, in each pass.
BOOST_AUTO_TEST_CASE(test_parallel_progress) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ProjectSession session = createProject(dir, 3);

  const Tests::ScopedMaxThreads threads(4);
  const QThread* const caller = QThread::currentThread();
  bool inCaller = true;
  std::map<int, std::vector<int>> doneByPass;
  const StageSequence::ProcessAllResult result = session.stages->processAll(
      nullptr, session.outFileNameGen, [&](const int lastFilterIdx, const int done, int /*total*/) {
        inCaller = inCaller && (QThread::currentThread() == caller);
        doneByPass[lastFilterIdx].push_back(done);
        return true;
      });
  BOOST_CHECK(result.failures.empty());
  BOOST_CHECK(result.output);
  BOOST_CHECK(inCaller);
  BOOST_CHECK_EQUAL(doneByPass.size(), 3u);
  const std::vector<int> expected{1, 2, 3};
  for (const auto& [pass, done] : doneByPass) {
    BOOST_CHECK_EQUAL_COLLECTIONS(done.begin(), done.end(), expected.begin(), expected.end());
  }
}

// Cancelled while pages run side by side: those running are finished, no other starts, and a
// second run completes the output.
BOOST_AUTO_TEST_CASE(test_parallel_cancel) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ProjectSession session = createProject(dir, 8);

  const Tests::ScopedMaxThreads threads(4);
  const int outputIdx = session.stages->outputFilterIdx();
  const StageSequence::ProcessAllResult cancelled = session.stages->processAll(
      nullptr, session.outFileNameGen,
      [&](const int lastFilterIdx, int /*done*/, int /*total*/) { return lastFilterIdx != outputIdx; });
  BOOST_CHECK(cancelled.cancelled);
  BOOST_CHECK(!cancelled.output);
  BOOST_CHECK(cancelled.failures.empty());
  BOOST_CHECK_LT(outputFiles(session).size(), 8u);

  const StageSequence::ProcessAllResult secondRun = session.stages->processAll(nullptr, session.outFileNameGen);
  BOOST_CHECK(secondRun.failures.empty());
  BOOST_CHECK(secondRun.output);
  BOOST_CHECK_EQUAL(outputFiles(session).size(), 8u);
}

// The failures are told in the order of the pages, whichever page fails first.
BOOST_AUTO_TEST_CASE(test_parallel_error_order) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ProjectSession session = createProject(dir, 4);
  BOOST_REQUIRE(Tests::writeFile(dir.filePath(QStringLiteral("page1.png")), "not an image"));
  BOOST_REQUIRE(Tests::writeFile(dir.filePath(QStringLiteral("page3.png")), "not an image"));

  const Tests::ScopedMaxThreads threads(4);
  const StageSequence::ProcessAllResult result = session.stages->processAll(nullptr, session.outFileNameGen);
  BOOST_REQUIRE_EQUAL(result.failures.size(), 2u);
  BOOST_CHECK(!result.output);
  BOOST_CHECK(result.failures[0].page.imageId().filePath().endsWith(QStringLiteral("page1.png")));
  BOOST_CHECK(result.failures[1].page.imageId().filePath().endsWith(QStringLiteral("page3.png")));
}

BOOST_AUTO_TEST_SUITE_END()
