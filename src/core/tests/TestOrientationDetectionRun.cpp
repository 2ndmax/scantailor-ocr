#include <filters/fix_orientation/OrientationDetection.h>
#include <filters/fix_orientation/Settings.h>

#include <CompositeCacheDrivenTask.h>
#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSequence.h>
#include <PageView.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>

#include <QDir>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <set>
#include <vector>

#include "TestHelpers.h"

namespace {
ProjectSession createProject(const QTemporaryDir& dir,
                             const int numPages,
                             const int rotation = 0,
                             const int dpi = 300) {
  std::vector<ImageFileInfo> files;
  for (int i = 0; i < numPages; ++i) {
    files.push_back(Tests::saveTextPage(dir.filePath(QStringLiteral("page%1.png").arg(i)), rotation, dpi));
  }
  const QString outDir = dir.filePath(QStringLiteral("out"));
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  return ProjectSession::createNew(ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()),
                                   outDir, QString(), startValues);
}

std::vector<PageInfo> pageList(const ProjectSession& session) {
  std::vector<PageInfo> pages;
  for (const PageInfo& page : session.pages->toPageSequence(IMAGE_VIEW)) {
    pages.push_back(page);
  }
  return pages;
}

ImageFileInfo blankPage(const QString& path, const int width, const int height, const int dpi) {
  QImage page(width, height, QImage::Format_RGB32);
  page.fill(Qt::white);
  if (dpi > 0) {
    const Dpm dpm(Dpi(dpi, dpi));
    page.setDotsPerMeterX(dpm.horizontal());
    page.setDotsPerMeterY(dpm.vertical());
  }
  if (!page.save(path)) {
    return ImageFileInfo(QFileInfo(path), {});
  }
  return ImageFileInfo(QFileInfo(path), {ImageMetadata(page.size(), Dpi(dpi, dpi))});
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OrientationDetectionRunSuite)

BOOST_AUTO_TEST_CASE(only_selection_set) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  ProjectSession session = createProject(dir, 3, 90);
  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();

  std::set<PageId> selection;
  selection.insert(pages[0].id());
  selection.insert(pages[2].id());

  const NullTaskStatus status;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, false, status);
  BOOST_CHECK_EQUAL(result.detected, 2);
  BOOST_CHECK_EQUAL(result.changed, 2);
  // selected pages got the detected correction
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[0].imageId()).toDegrees(), 270);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[2].imageId()).toDegrees(), 270);
  // unselected page keeps 0
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[1].imageId()).toDegrees(), 0);
}

BOOST_AUTO_TEST_CASE(book_rule_unselected_participate_not_set) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  // 5 rotated (90) pages + one blank page of the same format, all 2100x800.
  std::vector<ImageFileInfo> files;
  for (int i = 0; i < 5; ++i) {
    files.push_back(Tests::saveTextPage(dir.filePath(QStringLiteral("page%1.png").arg(i)), 90, 300));
  }
  // blank page with the same dimensions as the rotated text page
  QImage sample = Tests::makeTextPage(90, 300);
  files.push_back(blankPage(dir.filePath(QStringLiteral("blank.png")), sample.width(), sample.height(), 300));

  const QString outDir = dir.filePath(QStringLiteral("out"));
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);

  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();

  // a non-selected page keeps a pre-set rotation
  settings->applyRotation(pages[0].imageId(), OrthogonalRotation(90));
  const int preset = settings->getRotationFor(pages[0].imageId()).toDegrees();

  std::set<PageId> selection;
  selection.insert(pages.back().id());  // only the blank page may be set

  const NullTaskStatus status;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, true, status);

  BOOST_CHECK_EQUAL(static_cast<int>(result.results.size()), 6);
  // blank page got the book rotation
  const auto blankResult = result.results.back().second;
  BOOST_CHECK(blankResult.status == fix_orientation::OrientationDetection::PageResult::BOOK_RULE);
  BOOST_CHECK_EQUAL(blankResult.angle, 270);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages.back().imageId()).toDegrees(), 270);
  // non-selected pages keep their stored rotation
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[0].imageId()).toDegrees(), preset);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[1].imageId()).toDegrees(), 0);
}

BOOST_AUTO_TEST_CASE(undecided_keeps_rotation) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  // a single blank page
  QImage sample(800, 1000, QImage::Format_RGB32);
  sample.fill(Qt::white);
  std::vector<ImageFileInfo> files;
  files.push_back(blankPage(dir.filePath(QStringLiteral("blank.png")), 800, 1000, 300));

  const QString outDir = dir.filePath(QStringLiteral("out"));
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);

  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();
  settings->applyRotation(pages[0].imageId(), OrthogonalRotation(90));
  const int before = settings->getRotationFor(pages[0].imageId()).toDegrees();

  std::set<PageId> selection;
  selection.insert(pages[0].id());

  const NullTaskStatus status;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, false, status);
  BOOST_CHECK_EQUAL(result.undecided, 1);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[0].imageId()).toDegrees(), before);
}

BOOST_AUTO_TEST_CASE(unreadable_does_not_abort) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  ProjectSession session = createProject(dir, 2, 90);
  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();

  // corrupt the second file
  QFile f(dir.filePath(QStringLiteral("page1.png")));
  BOOST_REQUIRE(f.open(QIODevice::WriteOnly));
  f.write("not an image");
  f.close();

  std::set<PageId> selection;
  for (const auto& p : pages) {
    selection.insert(p.id());
  }

  const NullTaskStatus status;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, false, status);
  BOOST_CHECK(!result.cancelled);
  BOOST_CHECK_EQUAL(result.failed, 1);
  BOOST_CHECK_EQUAL(result.detected, 1);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[0].imageId()).toDegrees(), 270);
}

BOOST_AUTO_TEST_CASE(cancel_sets_nothing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  ProjectSession session = createProject(dir, 3, 90);
  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();
  std::vector<int> before;
  for (const auto& p : pages) {
    before.push_back(settings->getRotationFor(p.imageId()).toDegrees());
  }

  std::set<PageId> selection;
  for (const auto& p : pages) {
    selection.insert(p.id());
  }

  const NullTaskStatus status;
  int calls = 0;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, false, status, [&](int) {
    ++calls;
    return calls > 1;
  });
  BOOST_CHECK(result.cancelled);
  for (size_t i = 0; i < pages.size(); ++i) {
    BOOST_CHECK_EQUAL(settings->getRotationFor(pages[i].imageId()).toDegrees(), before[i]);
  }
}

BOOST_AUTO_TEST_CASE(double_page_processed_once) {
  // run always receives the image view (one entry per image), so a double page
  // cannot be passed twice. Instead this checks the image-id matching (4.5.3):
  // a selection given as a page-view id sets the rotation of its image.
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  ProjectSession session = createProject(dir, 2, 90);
  const std::vector<PageInfo> pages = pageList(session);
  const auto settings = session.stages->fixOrientationFilter()->settings();

  std::set<PageId> selection;
  selection.insert(PageId(pages[0].imageId(), PageId::LEFT_PAGE));

  const NullTaskStatus status;
  const auto result = fix_orientation::OrientationDetection::run(settings, pages, selection, false, status);
  BOOST_CHECK_EQUAL(result.detected, 1);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[0].imageId()).toDegrees(), 270);
  BOOST_CHECK_EQUAL(settings->getRotationFor(pages[1].imageId()).toDegrees(), 0);
}

BOOST_AUTO_TEST_SUITE_END()