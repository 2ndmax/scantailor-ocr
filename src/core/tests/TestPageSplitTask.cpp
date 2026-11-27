// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <ImageInfo.h>
#include <ImageMetadata.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <OrthogonalRotation.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <filters/page_split/Dependencies.h>
#include <filters/page_split/PageLayout.h>
#include <filters/page_split/Params.h>
#include <filters/page_split/Settings.h>
#include <filters/page_split/Task.h>
#include <filters/page_split/Utils.h>

#include <QImage>
#include <QLineF>
#include <QPainter>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <memory>
#include <vector>

namespace Tests {
using namespace page_split;

namespace {
const ImageId IMAGE(QStringLiteral("spread.png"));
const QSize IMAGE_SIZE(2480, 3508);

FilterData whiteSpread() {
  QImage image(IMAGE_SIZE, QImage::Format_RGB32);
  image.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return FilterData(image);
}

double area(const QPolygonF& poly) {
  double twiceArea = 0.0;
  for (int i = 0; i < poly.size(); ++i) {
    const QPointF& p = poly[i];
    const QPointF& q = poly[(i + 1) % poly.size()];
    twiceArea += p.x() * q.y() - q.x() * p.y();
  }
  return std::abs(twiceArea) / 2.0;
}

// Whether the page keeps at least a pixel of the image. The page area is built as
// Task::process() builds it; a sliver without area made the Output step fail.
bool keepsPartOfImage(const PageLayout& layout, const PageId::SubPage page, const ImageTransformation& xform) {
  ImageTransformation pageXform(xform);
  pageXform.setPreCropArea(
      Utils::intersectPreCropWithPageOutline(layout.pageOutline(page).toPolygon(), xform.preCropArea()));
  return area(pageXform.origPreCropArea().intersected(QPolygonF(pageXform.origRect()))) >= 1.0;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PageSplitTaskTestSuite)

// A split line from a project file that runs along the bottom edge of the image leaves one page
// only a sliver without area. The Output step then failed with "the cropping area is wrong"; Split
// Pages now finds the split line again instead.
BOOST_AUTO_TEST_CASE(stored_split_line_that_leaves_a_page_no_area_is_found_again) {
  const FilterData data(whiteSpread());
  const QRectF imageRect(data.xform().resultingRect());
  const PageLayout badLayout(imageRect, QLineF(QPointF(1e7, 0), QPointF(1240, 3508)));
  BOOST_REQUIRE(!keepsPartOfImage(badLayout, PageId::RIGHT_PAGE, data.xform())
                || !keepsPartOfImage(badLayout, PageId::LEFT_PAGE, data.xform()));

  const Dependencies deps(IMAGE_SIZE, data.xform().preRotation(), data.xform().origPreCropArea(), TWO_PAGES);
  auto settings = std::make_shared<Settings>();
  Settings::UpdateAction update;
  update.setLayoutType(TWO_PAGES);
  update.setParams(Params(badLayout, deps, MODE_MANUAL));
  settings->updatePage(IMAGE, update);

  const ImageMetadata metadata(IMAGE_SIZE, Dpi(300, 300));
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{ImageInfo(IMAGE, metadata, 2, false, false)},
                                              Qt::LeftToRight);
  const PageSequence sequence = pages->toPageSequence(PAGE_VIEW);
  BOOST_REQUIRE_EQUAL(sequence.numPages(), 2u);
  Task task(nullptr, settings, pages, nullptr, sequence.pageAt(1), true, false);
  task.process(NullTaskStatus(), data);

  const Settings::Record record = settings->getPageRecord(IMAGE);
  BOOST_REQUIRE(record.params());
  const PageLayout& layout = record.params()->pageLayout();
  BOOST_CHECK(layout.type() == PageLayout::TWO_PAGES);
  BOOST_CHECK(keepsPartOfImage(layout, PageId::LEFT_PAGE, data.xform()));
  BOOST_CHECK(keepsPartOfImage(layout, PageId::RIGHT_PAGE, data.xform()));
}

BOOST_AUTO_TEST_CASE(page_plus_offcut_survives_second_task_run) {
  {
    const FilterData data(whiteSpread());
    auto settings = std::make_shared<Settings>();
    Settings::UpdateAction update;
    update.setLayoutType(PAGE_PLUS_OFFCUT);
    settings->updatePage(IMAGE, update);

    const ImageMetadata metadata(IMAGE_SIZE, Dpi(300, 300));
    auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{ImageInfo(IMAGE, metadata, 1, false, false)},
                                                Qt::LeftToRight);
    const PageSequence sequence = pages->toPageSequence(PAGE_VIEW);
    Task task(nullptr, settings, pages, nullptr, sequence.pageAt(0), false, false);
    task.process(NullTaskStatus(), data);
    task.process(NullTaskStatus(), data);

    const Settings::Record record = settings->getPageRecord(IMAGE);
    BOOST_REQUIRE(record.params());
    BOOST_CHECK(record.combinedLayoutType() == PAGE_PLUS_OFFCUT);
    BOOST_CHECK(record.params()->pageLayout().type() == PageLayout::SINGLE_PAGE_CUT);
  }

  {
    QImage offcutImage(IMAGE_SIZE, QImage::Format_RGB32);
    offcutImage.fill(Qt::white);
    const Dpm dpm(Dpi(300, 300));
    offcutImage.setDotsPerMeterX(dpm.horizontal());
    offcutImage.setDotsPerMeterY(dpm.vertical());
    {
      QPainter painter(&offcutImage);
      painter.fillRect(QRect(20, 400, 100, 2700), Qt::black);
      painter.fillRect(QRect(340, 400, 1900, 2700), Qt::black);
    }
    const FilterData data(offcutImage);
    auto settings = std::make_shared<Settings>();
    Settings::UpdateAction update;
    update.setLayoutType(PAGE_PLUS_OFFCUT);
    settings->updatePage(IMAGE, update);

    const ImageMetadata metadata(IMAGE_SIZE, Dpi(300, 300));
    auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{ImageInfo(IMAGE, metadata, 1, false, false)},
                                                Qt::LeftToRight);
    const PageSequence sequence = pages->toPageSequence(PAGE_VIEW);
    Task task(nullptr, settings, pages, nullptr, sequence.pageAt(0), false, false);
    task.process(NullTaskStatus(), data);
    task.process(NullTaskStatus(), data);

    const Settings::Record record = settings->getPageRecord(IMAGE);
    BOOST_REQUIRE(record.params());
    const PageLayout& layout = record.params()->pageLayout();
    BOOST_CHECK(record.combinedLayoutType() == PAGE_PLUS_OFFCUT);
    BOOST_CHECK(layout.type() == PageLayout::SINGLE_PAGE_CUT);
    BOOST_CHECK_GT(layout.inscribedCutterLine(0).p1().x(), 50.0);
    BOOST_CHECK_LT(layout.inscribedCutterLine(0).p1().x(), 340.0);
  }
}

BOOST_AUTO_TEST_CASE(two_pages_auto_split_reestimates_after_rotation) {
  QImage image(IMAGE_SIZE, QImage::Format_RGB32);
  image.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  {
    QPainter painter(&image);
    painter.fillRect(QRect(150, 300, 700, 2900), Qt::black);
    painter.fillRect(QRect(1050, 300, 1250, 2900), Qt::black);
  }
  const FilterData baseData(image);
  auto settings = std::make_shared<Settings>();
  Settings::UpdateAction update;
  update.setLayoutType(TWO_PAGES);
  settings->updatePage(IMAGE, update);

  const ImageMetadata metadata(IMAGE_SIZE, Dpi(300, 300));
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>{ImageInfo(IMAGE, metadata, 2, false, false)},
                                              Qt::LeftToRight);
  const PageSequence sequence = pages->toPageSequence(PAGE_VIEW);
  Task task(nullptr, settings, pages, nullptr, sequence.pageAt(1), true, false);
  task.process(NullTaskStatus(), baseData);

  const Settings::Record before = settings->getPageRecord(IMAGE);
  BOOST_REQUIRE(before.params());
  BOOST_REQUIRE(before.params()->splitLineMode() == MODE_AUTO);
  const double unrotatedX = before.params()->pageLayout().inscribedCutterLine(0).pointAt(0.5).x();
  BOOST_REQUIRE_LT(unrotatedX, 1100.0);

  ImageTransformation rotatedXform = baseData.xform();
  OrthogonalRotation rot;
  rot.nextClockwiseDirection();
  rotatedXform.setPreRotation(rot);
  task.process(NullTaskStatus(), FilterData(baseData, rotatedXform));

  const Settings::Record after = settings->getPageRecord(IMAGE);
  BOOST_REQUIRE(after.params());
  BOOST_CHECK(after.params()->pageLayout().type() == PageLayout::TWO_PAGES);
  const double rotatedX = after.params()->pageLayout().inscribedCutterLine(0).pointAt(0.5).x();
  BOOST_CHECK_GT(rotatedX, 1600.0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
