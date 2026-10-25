// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_fix_orientation_qt.h"

#include <DefaultParams.h>
#include <FileNameDisambiguator.h>
#include <FilterData.h>
#include <FilterResult.h>
#include <FilterUiInterface.h>
#include <ImageInfo.h>
#include <ImageViewBase.h>
#include <NullTaskStatus.h>
#include <OutputFileNameGenerator.h>
#include <PageInfo.h>
#include <PageOrientationCollector.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <SelectedPage.h>
#include <filters/fix_orientation/CacheDrivenTask.h>
#include <filters/fix_orientation/Filter.h>
#include <filters/fix_orientation/OptionsWidget.h>
#include <filters/fix_orientation/Settings.h>
#include <filters/fix_orientation/Task.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QImage>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <vector>

using namespace fix_orientation;

namespace {
QDomDocument readDocument(const QString& filePath) {
  QDomDocument doc;
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    doc.setContent(&file);
  }
  return doc;
}

PageInfo pageInfo(const ImageId& imageId, const QSize& size) {
  return PageInfo(PageId(imageId), ImageMetadata(size, Dpi(300, 300)), 1, false, false);
}

OrthogonalRotation quarterTurns(const int count) {
  OrthogonalRotation rotation;
  for (int i = 0; i < count; ++i) {
    rotation.nextClockwiseDirection();
  }
  return rotation;
}

QSpinBox* spinBox(const QWidget& widget, const char* name) {
  return widget.findChild<QSpinBox*>(name);
}

// Keeps the image widget that a filter result shows.
class RecordingUi : public FilterUiInterface {
 public:
  void setOptionsWidget(FilterOptionsWidget*, Ownership) override {}

  void setImageWidget(QWidget* widget, const Ownership ownership, DebugImages*, bool) override {
    if (ownership == TRANSFER_OWNERSHIP) {
      imageWidget.reset(widget);
    }
  }

  void invalidateThumbnail(const PageId&) override {}

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
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const QString firstFile = dir.absoluteFilePath("first.ScanTailor");
  const QString secondFile = dir.absoluteFilePath("second.ScanTailor");
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), dir.absoluteFilePath("out"),
                                       Qt::LeftToRight);

  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  auto pages = std::make_shared<ProjectPages>(
      std::vector<ImageInfo>{ImageInfo(ImageId(dir.absoluteFilePath("scan.png"), 0), metadata, 1, false, false)},
      Qt::LeftToRight);
  QVERIFY(ProjectWriter(pages, SelectedPage(), outGen).write(firstFile, {}, ProcessingOptions(), DefaultParams()));

  QDomDocument doc = readDocument(firstFile);
  QDomElement projectEl = doc.documentElement();
  const QString imageId = projectEl.namedItem("images").firstChildElement("image").attribute("id");
  QVERIFY(!imageId.isEmpty());
  QDomElement filtersEl = projectEl.namedItem("filters").toElement();
  if (filtersEl.isNull()) {
    filtersEl = doc.createElement("filters");
    projectEl.appendChild(filtersEl);
  }
  QDomElement fixOrientationEl = doc.createElement("fix-orientation");
  QDomElement imageEl = doc.createElement("image");
  imageEl.setAttribute("id", imageId);
  imageEl.appendChild(OrthogonalRotation().toXml(doc, "rotation"));
  fixOrientationEl.appendChild(imageEl);
  filtersEl.appendChild(fixOrientationEl);

  const ProjectReader reader(doc, firstFile);
  QVERIFY(reader.success());
  auto filter = std::make_shared<Filter>(PageSelectionAccessor(nullptr));
  reader.readFilterSettings({filter});
  QVERIFY(ProjectWriter(reader.pages(), SelectedPage(), outGen)
              .write(secondFile, {filter}, ProcessingOptions(), DefaultParams()));

  const QDomElement rotationEl = readDocument(secondFile)
                                     .documentElement()
                                     .namedItem("filters")
                                     .namedItem("fix-orientation")
                                     .firstChildElement("image")
                                     .firstChildElement("rotation");
  QVERIFY(!rotationEl.isNull());
  QCOMPARE(OrthogonalRotation(rotationEl).toDegrees(), 0);
}

void TestFixOrientationQt::testTrimFieldsFollowRotation() {
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
  QSpinBox* const top = spinBox(widget, "trimTopSpin");
  QSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QSpinBox* const right = spinBox(widget, "trimRightSpin");
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
  QSpinBox* const right = spinBox(widget, "trimRightSpin");
  QSpinBox* const top = spinBox(widget, "trimTopSpin");
  QVERIFY(right != nullptr);
  QVERIFY(top != nullptr);
  QCOMPARE(right->value(), 1500);

  top->setValue(5);
  QCOMPARE(settings->getTrim(second).right, 1500);
  QCOMPARE(settings->getTrim(second).top, 5);
}

void TestFixOrientationQt::testTrimFieldsShowWhatIsComputed() {
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
  QSpinBox* const left = spinBox(widget, "trimLeftSpin");
  QSpinBox* const right = spinBox(widget, "trimRightSpin");
  QVERIFY(left != nullptr);
  QVERIFY(right != nullptr);
  const QRect inner = trim.toInnerRect(QSize(400, 300));
  QCOMPARE(left->value(), inner.left());
  QCOMPARE(right->value(), 400 - inner.left() - inner.width());
  QCOMPARE(left->value() + right->value() + inner.width(), 400);
}

void TestFixOrientationQt::testViewFollowsChangesDuringLoading() {
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
  QSpinBox* const top = spinBox(*widget, "trimTopSpin");
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
