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

  // After the Margins pass all pages have their geometry, so each one is detected; the callback
  // names the pages in their order, with the mode that Result::detected has for them.
  std::vector<std::pair<PageId, output::ColorMode>> reported;
  const output::ColorModeDetection::Result result = output::ColorModeDetection::run(
      session.stages->outputFilter(), pages, geometryTask, status, {},
      [&](const PageInfo& page, const output::ColorModeDetection::PageResult& pageResult) {
        BOOST_CHECK(pageResult.status == output::ColorModeDetection::PageResult::DETECTED);
        reported.emplace_back(page.id(), pageResult.mode);
      });
  BOOST_CHECK_EQUAL(result.skipped, 0);
  BOOST_CHECK_EQUAL(result.failed, 0);
  BOOST_REQUIRE_EQUAL(reported.size(), pages.size());
  BOOST_REQUIRE_EQUAL(result.detected.size(), pages.size());
  for (size_t i = 0; i < pages.size(); ++i) {
    BOOST_CHECK(reported[i].first == pages[i].id());
    BOOST_CHECK(reported[i].first == result.detected[i].first);
    BOOST_CHECK(reported[i].second == result.detected[i].second);
  }
}

// A page without the geometry of the steps before is skipped, and the callback says so.
BOOST_AUTO_TEST_CASE(callback_tells_a_skipped_page) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ProjectSession session = createProject(dir, 2);
  // Nothing processed: no page has its geometry yet.
  std::vector<PageInfo> pages;
  for (const PageInfo& page : session.pages->toPageSequence(PAGE_VIEW)) {
    pages.push_back(page);
  }
  const std::shared_ptr<CompositeCacheDrivenTask> geometryTask(
      session.stages->createCacheDrivenTask(session.stages->outputFilterIdx(), session.outFileNameGen));
  const NullTaskStatus status;

  std::vector<PageId> skipped;
  const output::ColorModeDetection::Result result = output::ColorModeDetection::run(
      session.stages->outputFilter(), pages, geometryTask, status, {},
      [&](const PageInfo& page, const output::ColorModeDetection::PageResult& pageResult) {
        if (pageResult.status == output::ColorModeDetection::PageResult::SKIPPED) {
          skipped.push_back(page.id());
        }
      });
  BOOST_CHECK_EQUAL(result.skipped, 2);
  BOOST_REQUIRE_EQUAL(skipped.size(), 2u);
  BOOST_CHECK(skipped[0] == pages[0].id());
  BOOST_CHECK(skipped[1] == pages[1].id());
}

BOOST_AUTO_TEST_SUITE_END()
