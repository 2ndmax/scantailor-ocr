// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <CompositeCacheDrivenTask.h>
#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <NullTaskStatus.h>
#include <PageInfo.h>
#include <PageSequence.h>
#include <PageView.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>
#include <filters/output/ColorModeDetection.h>

#include <QDir>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

#include "TestHelpers.h"

namespace {
ProjectSession createProject(const QTemporaryDir& dir, const int numPages) {
  std::vector<ImageFileInfo> files;
  for (int i = 0; i < numPages; ++i) {
    files.push_back(Tests::saveTextPageScan(dir.filePath(QStringLiteral("page%1.png").arg(i)), 300));
  }
  const QString outDir = dir.filePath(QStringLiteral("out"));
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  return ProjectSession::createNew(ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()),
                                   outDir, QString(), startValues);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ColorModeDetectionProgressSuite)

BOOST_AUTO_TEST_CASE(callback_called_per_page) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ProjectSession session = createProject(dir, 3);

  session.stages->processAll(nullptr, session.outFileNameGen, session.stages->pageLayoutFilterIdx());

  std::vector<PageInfo> pages;
  for (const PageInfo& page : session.pages->toPageSequence(PAGE_VIEW)) {
    pages.push_back(page);
  }
  const std::shared_ptr<CompositeCacheDrivenTask> geometryTask(
      session.stages->createCacheDrivenTask(session.stages->outputFilterIdx(), session.outFileNameGen));
  const NullTaskStatus status;

  int callbackCount = 0;
  const output::ColorModeDetection::Result result
      = output::ColorModeDetection::run(session.stages->outputFilter(), pages, geometryTask, status, {},
                                        [&](const PageInfo& page, const output::ColorModeDetection::PageResult& r) {
                                          ++callbackCount;
                                          BOOST_CHECK((r.status == output::ColorModeDetection::PageResult::DETECTED)
                                                      || (r.status == output::ColorModeDetection::PageResult::SKIPPED));
                                        });
  BOOST_CHECK_EQUAL(callbackCount, 3);
  BOOST_CHECK_EQUAL(static_cast<int>(result.detected.size()) + result.skipped, 3);
}

BOOST_AUTO_TEST_SUITE_END()