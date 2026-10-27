// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_select_content_qt.h"

#include <DefaultParams.h>
#include <Dpi.h>
#include <ImageMetadata.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSelectionAccessor.h>
#include <filters/select_content/Dependencies.h>
#include <filters/select_content/DetectionOptions.h>
#include <filters/select_content/OptionsWidget.h>
#include <filters/select_content/Params.h>
#include <filters/select_content/Settings.h>

#include <QPolygonF>
#include <QSpinBox>
#include <QtTest/QtTest>
#include <memory>
#include <optional>

using namespace select_content;

namespace {
const PageId PAGE(ImageId("page.png"));

QPolygonF outline() {
  QPolygonF polygon;
  polygon << QPointF(0, 0) << QPointF(600, 0) << QPointF(600, 800) << QPointF(0, 800);
  return polygon;
}

/** A page whose content box was detected automatically with \p detectedWith. */
OptionsWidget::UiData detectedPage(const std::optional<DetectionOptions>& detectedWith) {
  OptionsWidget::UiData data;
  data.setContentRect(QRectF(50, 50, 500, 700));
  data.setPageRect(QRectF(0, 0, 600, 800));
  data.setContentDetectionMode(MODE_AUTO);
  data.setPageDetectionMode(MODE_DISABLED);
  data.setDetectionOptions(detectedWith);
  data.setDependencies(Dependencies(outline(), MODE_AUTO, MODE_DISABLED, false, detectedWith));
  return data;
}

/** Whether the task would detect the content box again, as it decides in Task::process(). */
bool contentBoxDetectedAgain(const Settings& settings) {
  const std::unique_ptr<Params> params(settings.getPageParams(PAGE));
  if (!params) {
    return false;
  }
  const Dependencies current(outline(), params->contentDetectionMode(), params->pageDetectionMode(),
                             params->isFineTuningEnabled(), params->detectionOptions());
  bool updateContentBox = false;
  bool updatePageBox = false;
  current.compatibleWith(params->dependencies(), &updateContentBox, &updatePageBox);
  return updateContentBox;
}

struct Panel {
  std::shared_ptr<Settings> settings = std::make_shared<Settings>();
  OptionsWidget widget{settings, std::make_shared<DefaultParams>(), PageSelectionAccessor(nullptr)};

  explicit Panel(const OptionsWidget::UiData& data) {
    widget.preUpdateUI(PageInfo(PAGE, ImageMetadata(QSize(600, 800), Dpi(300, 300)), 1, false, false));
    widget.postUpdateUI(data);
  }

  QSpinBox* fillFactor() const { return widget.findChild<QSpinBox*>("fillFactorSpinBox"); }
};
}  // namespace

void TestSelectContentQt::testChangedDetectionOptionsDetectAgain() {
  // Other settings than the box was detected with make the task detect it again; going back to
  // them does not, as the box already matches them.
  const DetectionOptions detectedWith(70, 0, false);
  Panel panel(detectedPage(detectedWith));
  QVERIFY(panel.fillFactor() != nullptr);

  panel.fillFactor()->setValue(80);
  QVERIFY(contentBoxDetectedAgain(*panel.settings));
  panel.fillFactor()->setValue(70);
  QVERIFY(!contentBoxDetectedAgain(*panel.settings));
}

void TestSelectContentQt::testOldPagesDetectAgainOnlyWithOtherOptions() {
  // Pages detected before the settings existed have none stored; they were detected with the
  // defaults.
  Panel panel(detectedPage(std::nullopt));
  QVERIFY(panel.fillFactor() != nullptr);
  QCOMPARE(panel.fillFactor()->value(), DetectionOptions::DEFAULT_FILL_FACTOR);

  panel.fillFactor()->setValue(80);
  QVERIFY(contentBoxDetectedAgain(*panel.settings));
  panel.fillFactor()->setValue(DetectionOptions::DEFAULT_FILL_FACTOR);
  QVERIFY(!contentBoxDetectedAgain(*panel.settings));
}
