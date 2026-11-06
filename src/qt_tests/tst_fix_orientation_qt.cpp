// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_fix_orientation_qt.h"

#include <FilterData.h>
#include <FilterResult.h>
#include <FilterUiInterface.h>
#include <ImageViewBase.h>
#include <NullTaskStatus.h>
#include <PageInfo.h>
#include <PageOrientationCollector.h>
#include <PageSelectionAccessor.h>
#include <ProjectReader.h>
#include <UnitsProvider.h>
#include <filters/fix_orientation/CacheDrivenTask.h>
#include <filters/fix_orientation/Filter.h>
#include <filters/fix_orientation/OptionsWidget.h>
#include <filters/fix_orientation/Settings.h>
#include <filters/fix_orientation/Task.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QDomDocument>
#include <QImage>
#include <QSpinBox>
#include <QtTest/QtTest>
#include <memory>
#include <vector>

#include "tst_project_xml.h"

using namespace fix_orientation;

namespace {
PageInfo pageInfo(const ImageId& imageId, const QSize& size, const Dpi& dpi = Dpi(300, 300)) {
  return PageInfo(PageId(imageId), ImageMetadata(size, dpi), 1, false, false);
}

// Sets the unit of the length fields for one test and restores the previous one.
class ScopedUnits {
 public:
  explicit ScopedUnits(const Units units) : m_previous(UnitsProvider::getInstance().getUnits()) {
    UnitsProvider::getInstance().setUnits(units);
  }

  ~ScopedUnits() { UnitsProvider::getInstance().setUnits(m_previous); }

  ScopedUnits(const ScopedUnits&) = delete;
  ScopedUnits& operator=(const ScopedUnits&) = delete;

 private:
  const Units m_previous;
};

OrthogonalRotation quarterTurns(const int count) {
  OrthogonalRotation rotation;
  for (int i = 0; i < count; ++i) {
    rotation.nextClockwiseDirection();
  }
  return rotation;
}

QDoubleSpinBox* spinBox(const QWidget& widget, const char* name) {
  return widget.findChild<QDoubleSpinBox*>(name);
}

// Keeps the image widget that a filter result shows.
class RecordingUi : public FilterUiInterface {
 public:
  void setOptionsWidget(FilterOptionsWidget* /*widget*/, Ownership /*ownership*/) override {}

  void setImageWidget(QWidget* widget,
                      const Ownership ownership,
                      DebugImages* /*debugImages*/,
                      bool /*overlay*/) override {
    if (ownership == TRANSFER_OWNERSHIP) {
      imageWidget.reset(widget);
    }
  }

  void invalidateThumbnail(const PageId& /*pageId*/) override {}

  void invalidateAllThumbnails() override {}

  std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester() override { return nullptr; }

  std::unique_ptr<QWidget> imageWidget;
};

class RotationCollector : public PageOrientationCollector {
 public:
  void process(const OrthogonalRotation& orientation) override { rotation = orientation; }

  OrthogonalRotation rotation;
};
}  // namespace

void TestFixOrientationQt::testZeroRotationIsSaved() {
  // An image without a saved rotation gets the default rotation when loaded, so a rotation of 0
  // must be saved too.
  const auto makeFixOrientationEl = [](QDomDocument& doc, const QString& imageId) {
    QDomElement fixOrientationEl = doc.createElement("fix-orientation");
    QDomElement imageEl = doc.createElement("image");
    imageEl.setAttribute("id", imageId);
    imageEl.appendChild(OrthogonalRotation().toXml(doc, "rotation"));
    fixOrientationEl.appendChild(imageEl);
    return fixOrientationEl;
  };
  const auto makeFilter = [](const ProjectReader&) { return std::make_shared<Filter>(PageSelectionAccessor(nullptr)); };

  const QDomElement rotationEl = ProjectXml::saveAndReload("fix-orientation", makeFixOrientationEl, makeFilter)
                                     .firstChildElement("image")
                                     .firstChildElement("rotation");
  QVERIFY(!rotationEl.isNull());
  QCOMPARE(OrthogonalRotation(rotationEl).toDegrees(), 0);
}

void TestFixOrientationQt::testTrimFieldsFollowRotation() {
  const ScopedUnits pixelUnits(PIXELS);
  // The trim fields refer to the view; the trim is saved for the source image.
  auto settings = std::make_shared<Settings>();
  const ImageId image("page.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 10;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  // A quarter turn clockwise shows the left edge at the top.
  widget.preUpdateUI(pageInfo(image, QSize(400, 300)), quarterTurns(1));
  QDoubleSpinBox* const top = spinBox(widget, "trimTopSpin");
  QDoubleSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QDoubleSpinBox* const right = spinBox(widget, "trimRightSpin");
  QVERIFY(top != nullptr);
  QVERIFY(left != nullptr);
  QVERIFY(right != nullptr);
  QCOMPARE(top->value(), 10);
  QCOMPARE(left->value(), 0);

  // ... and the top edge at the right.
  right->setValue(20);
  const ImageTrim saved(settings->getTrim(image));
  QCOMPARE(saved.left, 10);
  QCOMPARE(saved.top, 20);
  QCOMPARE(saved.right, 0);
  QCOMPARE(saved.bottom, 0);
}

void TestFixOrientationQt::testTrimFieldsKeepValuesOnPageSwitch() {
  const ScopedUnits pixelUnits(PIXELS);
  // The maximum of a field depends on the opposite field. Those of the previous page must not cut
  // the values of the next one.
  auto settings = std::make_shared<Settings>();
  const ImageId first("first.png");
  const ImageId second("second.png");
  ImageTrim firstTrim;
  firstTrim.enabled = true;
  firstTrim.left = 3000;
  settings->setTrim(first, firstTrim);
  ImageTrim secondTrim;
  secondTrim.enabled = true;
  secondTrim.right = 1500;
  settings->setTrim(second, secondTrim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(first, QSize(4000, 3000)), OrthogonalRotation());
  widget.preUpdateUI(pageInfo(second, QSize(4000, 3000)), OrthogonalRotation());
  QDoubleSpinBox* const right = spinBox(widget, "trimRightSpin");
  QDoubleSpinBox* const top = spinBox(widget, "trimTopSpin");
  QVERIFY(right != nullptr);
  QVERIFY(top != nullptr);
  QCOMPARE(right->value(), 1500);

  top->setValue(5);
  QCOMPARE(settings->getTrim(second).right, 1500);
  QCOMPARE(settings->getTrim(second).top, 5);
}

void TestFixOrientationQt::testTrimFieldsShowWhatIsComputed() {
  const ScopedUnits pixelUnits(PIXELS);
  // A trim that does not fit the image, e.g. after the image was replaced by a smaller one: the fields
  // show the values that are used (ImageTrim::toInnerRect() redistributes them), not cut ones.
  auto settings = std::make_shared<Settings>();
  const ImageId image("small.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 3000;
  trim.right = 3000;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(image, QSize(400, 300)), OrthogonalRotation());
  QDoubleSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QDoubleSpinBox* const right = spinBox(widget, "trimRightSpin");
  QVERIFY(left != nullptr);
  QVERIFY(right != nullptr);
  const QRect inner = trim.toInnerRect(QSize(400, 300));
  QCOMPARE(left->value(), inner.left());
  QCOMPARE(right->value(), 400 - inner.left() - inner.width());
  QCOMPARE(left->value() + right->value() + inner.width(), 400);
}

void TestFixOrientationQt::testViewFollowsChangesDuringLoading() {
  const ScopedUnits pixelUnits(PIXELS);
  // Rotating a page while it is processed must not be undone when it is shown, and the view must
  // show the rotation and the trim.
  auto filter = std::make_shared<Filter>(PageSelectionAccessor(nullptr));
  const ImageId image("page.png");
  const PageInfo info(pageInfo(image, QSize(400, 300)));
  RecordingUi ui;
  filter->preUpdateUI(&ui, info);

  QImage pixels(400, 300, QImage::Format_RGB32);
  pixels.fill(Qt::white);
  const FilterResultPtr result
      = filter->createTask(info.id(), nullptr, false)->process(NullTaskStatus(), FilterData(pixels));

  OptionsWidget* widget = filter->optionsWidget();
  QVERIFY(widget != nullptr);
  auto* const rotateRight = widget->findChild<QAbstractButton*>("rotateRightBtn");
  auto* const trimEnabled = widget->findChild<QCheckBox*>("trimEnabledCheck");
  QDoubleSpinBox* const top = spinBox(*widget, "trimTopSpin");
  QVERIFY(rotateRight != nullptr);
  QVERIFY(trimEnabled != nullptr);
  QVERIFY(top != nullptr);
  rotateRight->click();
  result->updateUI(&ui);

  RotationCollector collector;
  filter->createCacheDrivenTask(nullptr)->process(info, &collector);
  QCOMPARE(collector.rotation.toDegrees(), 90);

  const auto* view = dynamic_cast<ImageViewBase*>(ui.imageWidget.get());
  QVERIFY(view != nullptr);
  QCOMPARE(view->virtualDisplayRect().size(), QSizeF(300, 400));

  trimEnabled->setChecked(true);
  top->setValue(10);
  QCOMPARE(view->virtualDisplayRect().size(), QSizeF(300, 390));
}

void TestFixOrientationQt::testTrimFieldsShowTheUnits() {
  // The trim fields show the unit of the program settings, converted with the resolution of the
  // image; the trim is still saved in pixels.
  const ScopedUnits millimeters(MILLIMETRES);
  auto settings = std::make_shared<Settings>();
  const ImageId image("page.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 300;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(image, QSize(4000, 3000)), OrthogonalRotation());
  QDoubleSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QDoubleSpinBox* const right = spinBox(widget, "trimRightSpin");
  QVERIFY(left != nullptr);
  QVERIFY(right != nullptr);
  QVERIFY(left->suffix().endsWith("mm"));
  QCOMPARE(left->value(), 25.4);  // 300 px at 300 dpi

  right->setValue(50.8);
  QCOMPARE(settings->getTrim(image).right, 600);
  QCOMPARE(settings->getTrim(image).left, 300);
}

void TestFixOrientationQt::testTrimFieldsKeepTheOtherEdges() {
  // A field shows a rounded value: 30 px at 600 dpi are 1.27 mm, shown as 1.3 mm. Changing another
  // field must not convert that rounded value back (it would give 31 px).
  const ScopedUnits millimeters(MILLIMETRES);
  auto settings = std::make_shared<Settings>();
  const ImageId image("page.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 30;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(image, QSize(4000, 3000), Dpi(600, 600)), OrthogonalRotation());
  QDoubleSpinBox* const top = spinBox(widget, "trimTopSpin");
  QVERIFY(top != nullptr);
  top->setValue(10.0);
  QCOMPARE(settings->getTrim(image).left, 30);
  QCOMPARE(settings->getTrim(image).top, 236);  // 10 mm at 600 dpi are 236.2 px
}

void TestFixOrientationQt::testTrimFieldsFollowAUnitChange() {
  const ScopedUnits pixelUnits(PIXELS);
  auto settings = std::make_shared<Settings>();
  const ImageId image("page.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.top = 600;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(image, QSize(4000, 3000)), OrthogonalRotation());
  QDoubleSpinBox* const top = spinBox(widget, "trimTopSpin");
  QVERIFY(top != nullptr);
  QCOMPARE(top->value(), 600.0);

  UnitsProvider::getInstance().setUnits(INCHES);
  QCOMPARE(top->value(), 2.0);
  QCOMPARE(settings->getTrim(image).top, 600);
}

void TestFixOrientationQt::testTrimFieldsShowPixelsWithoutResolution() {
  // Without a resolution there is nothing to convert with.
  const ScopedUnits millimeters(MILLIMETRES);
  auto settings = std::make_shared<Settings>();
  const ImageId image("page.png");
  ImageTrim trim;
  trim.enabled = true;
  trim.left = 40;
  settings->setTrim(image, trim);

  OptionsWidget widget(settings, PageSelectionAccessor(nullptr));
  widget.preUpdateUI(pageInfo(image, QSize(400, 300), Dpi()), OrthogonalRotation());
  QDoubleSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QVERIFY(left != nullptr);
  QVERIFY(left->suffix().endsWith("px"));
  QCOMPARE(left->value(), 40.0);
}
