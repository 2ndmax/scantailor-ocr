// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <AutoManualMode.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <filters/select_content/DetectionOptions.h>
#include <filters/select_content/Params.h>
#include <filters/select_content/Settings.h>
#include <filters/select_content/Task.h>

#include <QImage>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace Tests {
using namespace select_content;

namespace {
const PageId PAGE(ImageId(QStringLiteral("page.png")));

FilterData whitePage() {
  QImage image(600, 800, QImage::Format_RGB32);
  image.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return FilterData(image);
}

QRectF runTask(const Params& params, const FilterData& data) {
  auto settings = std::make_shared<Settings>();
  settings->setPageParams(PAGE, params);
  Task task(nullptr, nullptr, settings, PAGE, DetectionOptions(), true, false);
  task.process(NullTaskStatus(), data);
  const std::unique_ptr<Params> result(settings->getPageParams(PAGE));
  BOOST_REQUIRE(result);
  return result->pageRect();
}
}  // namespace

BOOST_AUTO_TEST_SUITE(SelectContentTaskTestSuite)

BOOST_AUTO_TEST_CASE(manual_start_page_box_is_centered) {
  // As built from the start values with a manual page box: a size, no position yet.
  const Params startParams(QRectF(), QSizeF(), QRectF(0, 0, 400, 500), Dependencies(), MODE_DISABLED, MODE_MANUAL,
                           false);
  const FilterData data(whitePage());
  const QRectF pageRect(runTask(startParams, data));
  const QRectF imageRect(data.xform().resultingRect());
  BOOST_CHECK_CLOSE(pageRect.center().x(), imageRect.center().x(), 1e-6);
  BOOST_CHECK_CLOSE(pageRect.center().y(), imageRect.center().y(), 1e-6);
  BOOST_CHECK_CLOSE(pageRect.width(), 400.0, 1e-6);
  BOOST_CHECK_CLOSE(pageRect.height(), 500.0, 1e-6);
}

BOOST_AUTO_TEST_CASE(manual_page_box_larger_than_the_image_is_cut) {
  const Params startParams(QRectF(), QSizeF(), QRectF(0, 0, 900, 1000), Dependencies(), MODE_DISABLED, MODE_MANUAL,
                           false);
  const FilterData data(whitePage());
  const QRectF pageRect(runTask(startParams, data));
  BOOST_CHECK(data.xform().resultingRect().contains(pageRect));
}

BOOST_AUTO_TEST_CASE(manual_page_box_follows_into_a_smaller_outline) {
  // The page outline became smaller (e.g. a moved split line): the box must not reach out of it.
  QPolygonF oldOutline(QRectF(0, 0, 900, 800));
  const Params params(QRectF(), QSizeF(), QRectF(100, 100, 700, 600), Dependencies(oldOutline), MODE_DISABLED,
                      MODE_MANUAL, false);
  const FilterData data(whitePage());
  const QRectF pageRect(runTask(params, data));
  BOOST_CHECK(data.xform().resultingRect().contains(pageRect));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
