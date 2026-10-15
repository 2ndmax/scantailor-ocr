// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_core_qt.h"

#include <FileNameDisambiguator.h>
#include <ImageInfo.h>
#include <OutputFileNameGenerator.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <SelectedPage.h>
#include <SmartFilenameOrdering.h>
#include <Units.h>
#include <filters/page_split/Filter.h>
#include <foundation/Utils.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <memory>
#include <vector>

void TestCoreQt::testUnitsToString() {
  QCOMPARE(unitsToString(PIXELS), QString("px"));
  QCOMPARE(unitsToString(MILLIMETRES), QString("mm"));
  QCOMPARE(unitsToString(CENTIMETRES), QString("cm"));
  QCOMPARE(unitsToString(INCHES), QString("in"));
}

void TestCoreQt::testUnitsFromString() {
  QCOMPARE(unitsFromString(QString("px")), PIXELS);
  QCOMPARE(unitsFromString(QString("mm")), MILLIMETRES);
  QCOMPARE(unitsFromString(QString("cm")), CENTIMETRES);
  QCOMPARE(unitsFromString(QString("in")), INCHES);
  QCOMPARE(unitsFromString(QString("unknown")), MILLIMETRES);
}

void TestCoreQt::testUnitsToLocalizedString() {
  QString px = unitsToLocalizedString(PIXELS);
  QString mm = unitsToLocalizedString(MILLIMETRES);
  QVERIFY(!px.isEmpty());
  QVERIFY(!mm.isEmpty());
}

void TestCoreQt::testUtilsDoubleToString() {
  QCOMPARE(foundation::Utils::doubleToString(0.0), QString("0"));
  QVERIFY(foundation::Utils::doubleToString(123.456).contains("123"));
  QVERIFY(foundation::Utils::doubleToString(-42.5).startsWith("-"));
}

void TestCoreQt::testSmartFilenameOrdering() {
  const SmartFilenameOrdering less;
  const QFileInfo a("/etc/1.png");
  const QFileInfo b("/etc/2.png");
  QVERIFY(less(a, b));
  QVERIFY(!less(b, a));
  QVERIFY(!less(a, a));

  const QFileInfo c("/etc/a_0002.png");
  const QFileInfo d("/etc/a_1.png");
  QVERIFY(less(d, c));
}

void TestCoreQt::testSignalSpy() {
  Emitter obj;
  QSignalSpy spy(&obj, &Emitter::valueChanged);
  QVERIFY(spy.isValid());
  QCOMPARE(spy.count(), 0);

  obj.doEmit(QString("hello"));
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toString(), QString("hello"));

  obj.doEmit(QString("world"));
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(0).toString(), QString("world"));
}

namespace {
QDomDocument readDocument(const QString& filePath) {
  QDomDocument doc;
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    doc.setContent(&file);
  }
  return doc;
}
}  // namespace

void TestCoreQt::testPageSplitKeepsLayoutTypeWithoutParams() {
  // A page type chosen for a page that wasn't processed yet has no split line (params).
  // It must still be saved.
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
  QVERIFY(ProjectWriter(pages, SelectedPage(), outGen).write(firstFile, {}));

  QDomDocument doc = readDocument(firstFile);
  QDomElement projectEl = doc.documentElement();
  const QString imageId = projectEl.namedItem("images").firstChildElement("image").attribute("id");
  QVERIFY(!imageId.isEmpty());
  QDomElement filtersEl = projectEl.namedItem("filters").toElement();
  if (filtersEl.isNull()) {
    filtersEl = doc.createElement("filters");
    projectEl.appendChild(filtersEl);
  }
  QDomElement pageSplitEl = doc.createElement("page-split");
  pageSplitEl.setAttribute("defaultLayoutType", "auto-detect");
  QDomElement imageEl = doc.createElement("image");
  imageEl.setAttribute("id", imageId);
  imageEl.setAttribute("layoutType", "single-uncut");
  pageSplitEl.appendChild(imageEl);
  filtersEl.appendChild(pageSplitEl);

  const ProjectReader reader(doc, firstFile);
  QVERIFY(reader.success());
  auto filter = std::make_shared<page_split::Filter>(reader.pages(), PageSelectionAccessor(nullptr));
  reader.readFilterSettings({filter});
  QVERIFY(ProjectWriter(reader.pages(), SelectedPage(), outGen).write(secondFile, {filter}));

  const QDomElement savedEl = readDocument(secondFile)
                                  .documentElement()
                                  .namedItem("filters")
                                  .namedItem("page-split")
                                  .firstChildElement("image");
  QVERIFY(!savedEl.isNull());
  QCOMPARE(savedEl.attribute("layoutType"), QString("single-uncut"));
}
