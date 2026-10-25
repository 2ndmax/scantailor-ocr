// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Dpm.h>
#include <FilterData.h>
#include <ImageSettings.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <filters/deskew/Dependencies.h>
#include <filters/deskew/Params.h>
#include <filters/deskew/Settings.h>
#include <filters/deskew/Task.h>

#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace Tests {
using namespace deskew;

namespace {
const PageId PAGE(ImageId(QStringLiteral("page.png")));

// Lines of dark bars on white, like text.
QImage textPage(const int dotsPerMeterX, const int dotsPerMeterY) {
  QImage image(600, 800, QImage::Format_RGB32);
  image.fill(Qt::white);
  QPainter painter(&image);
  for (int y = 60; y < 740; y += 30) {
    for (int x = 60; x < 540; x += 26) {
      painter.fillRect(x, y, 18, 14, Qt::black);
    }
  }
  painter.end();
  image.setDotsPerMeterX(dotsPerMeterX);
  image.setDotsPerMeterY(dotsPerMeterY);
  return image;
}

const int DPM_300 = Dpm(Dpi(300, 300)).horizontal();

// Runs the deskew task of PAGE on the image as if it came from "Split Pages".
std::unique_ptr<Params> runTask(const std::shared_ptr<Settings>& settings,
                                const FilterData& data,
                                const bool autoObliqueByDefault) {
  Task task(nullptr, settings, std::make_shared<ImageSettings>(), nullptr, PAGE, false, autoObliqueByDefault,
            DETECT_CONTENT, true, false);
  task.process(NullTaskStatus(), data);
  return settings->getPageParams(PAGE);
}

// Parameters measured under other dependencies, so that the task measures the page again.
Params outdatedParams(const AutoManualMode obliqueMode) {
  QPolygonF otherOutline;
  otherOutline << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10) << QPointF(0, 10);
  return Params(0.0, 1.5, Dependencies(otherOutline, OrthogonalRotation()), MODE_AUTO, obliqueMode);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(DeskewTaskTestSuite)

BOOST_AUTO_TEST_CASE(page_keeps_its_auto_oblique_when_measured_again) {
  // The start value of the project is "manual" (as after clicking Manual on another page before):
  // a page that has automatic oblique keeps it.
  auto settings = std::make_shared<Settings>();
  settings->setPageParams(PAGE, outdatedParams(MODE_AUTO));
  const std::unique_ptr<Params> params(runTask(settings, FilterData(textPage(DPM_300, DPM_300)), false));
  BOOST_REQUIRE(params);
  BOOST_CHECK(params->obliqueMode() == MODE_AUTO);
}

BOOST_AUTO_TEST_CASE(page_keeps_its_manual_oblique_when_measured_again) {
  auto settings = std::make_shared<Settings>();
  settings->setPageParams(PAGE, outdatedParams(MODE_MANUAL));
  const std::unique_ptr<Params> params(runTask(settings, FilterData(textPage(DPM_300, DPM_300)), true));
  BOOST_REQUIRE(params);
  BOOST_CHECK(params->obliqueMode() == MODE_MANUAL);
  BOOST_CHECK_CLOSE(params->obliqueAngle(), 1.5, 1e-9);
}

BOOST_AUTO_TEST_CASE(empty_image_area_still_stores_params) {
  // A crop area outside the image leaves nothing to measure; the page still needs a fixed state.
  auto settings = std::make_shared<Settings>();
  const FilterData original(textPage(DPM_300, DPM_300));
  ImageTransformation xform(original.xform());
  xform.setPreCropArea(QPolygonF(QRectF(2000, 2000, 100, 100)));
  const std::unique_ptr<Params> params(runTask(settings, FilterData(original, xform), true));
  BOOST_REQUIRE(params);
  BOOST_CHECK_EQUAL(params->deskewAngle(), 0.0);
  BOOST_CHECK_EQUAL(params->obliqueAngle(), 0.0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
