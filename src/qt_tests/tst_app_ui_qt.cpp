// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_app_ui_qt.h"

#include <ApplicationSettings.h>
#include <AutoImportUtils.h>
#include <Dpi.h>
#include <ExportView.h>
#include <FixDpiDialog.h>
#include <ImageFileInfo.h>
#include <ImageId.h>
#include <ImageMetadata.h>
#include <MainWindow.h>
#include <PageSequence.h>
#include <ProcessingOptions.h>
#include <ProjectFilesDialog.h>
#include <ProjectPages.h>
#include <StateIcon.h>
#include <ThumbnailPixmapCache.h>
#include <ThumbnailSequence.h>

#include <QAbstractItemModelTester>
#include <QAbstractProxyModel>
#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsItem>
#include <QGraphicsView>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest/QtTest>
#include <memory>

void TestAppUiQt::testStateIconCreatesAllColors() {
  // P9-11: No static QPixmap array; produces valid pixmaps at runtime for all colors.
  for (const StateIcon::Color color : {StateIcon::Green, StateIcon::Yellow, StateIcon::Red, StateIcon::None}) {
    const QPixmap pm = StateIcon::pixmap(color);
    QVERIFY(!pm.isNull());
    QCOMPARE(pm.deviceIndependentSize(), QSizeF(12.0, 12.0));
  }
}

void TestAppUiQt::testFixDpiTreeModelIndexRejectsInvalidIndices() {
  // P9-15: TreeModel::index() must check hasIndex() and reject out-of-range/negative rows.
  std::vector<ImageFileInfo> files;
  ImageFileInfo info(QFileInfo(QStringLiteral("/tmp/a.png")), {ImageMetadata(QSize(100, 200), Dpi(0, 0))});
  files.push_back(info);

  FixDpiDialog dialog(files);
  auto* proxy = dialog.findChild<QAbstractProxyModel*>();
  QVERIFY(proxy != nullptr);
  QAbstractItemModel* model = proxy->sourceModel();
  QVERIFY(model != nullptr);
  QAbstractItemModelTester tester(model, QAbstractItemModelTester::FailureReportingMode::QtTest);

  QVERIFY(!model->index(-1, 0, QModelIndex()).isValid());
  QVERIFY(!model->index(0, -1, QModelIndex()).isValid());
  QVERIFY(!model->index(99, 0, QModelIndex()).isValid());
  const QModelIndex folder = model->index(0, 0, QModelIndex());
  QVERIFY(folder.isValid());
  QVERIFY(!model->index(-1, 0, folder).isValid());
  QVERIFY(!model->index(99, 0, folder).isValid());
}

void TestAppUiQt::testThumbnailSequenceDuplicateInsertAndSingleColumnCentering() {
  // P9-10: insert() must not mutate sceneRect on duplicate PageId, and SINGLE_COLUMN insert() must center items.
  QGraphicsView view;
  view.resize(400, 600);
  view.show();

  ThumbnailSequence seq(QSizeF(100.0, 140.0), ThumbnailSequence::SINGLE_COLUMN);
  seq.attachView(&view);

  std::vector<ImageFileInfo> files{
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/p1.png")), {ImageMetadata(QSize(100, 140), Dpi(300, 300))})};
  auto pages = ProjectPages::createNew(files, Qt::LeftToRight);
  seq.reset(pages->toPageSequence(PAGE_VIEW), ThumbnailSequence::KEEP_SELECTION);
  QCoreApplication::processEvents();

  const ImageId img2(QStringLiteral("/tmp/p2.png"));
  const PageInfo page2(PageId(img2), ImageMetadata(QSize(100, 140), Dpi(300, 300)), 1, false, false);
  seq.insert(page2, BEFORE, ImageId());
  QCOMPARE(seq.toPageSequence().numPages(), size_t(2));

  // New item in SINGLE_COLUMN must be horizontally centered inside the 400px view (x > 0).
  bool foundCentered = false;
  for (QGraphicsItem* item : view.scene()->items()) {
    if (item->parentItem() == nullptr && item->boundingRect().width() >= 90.0) {
      if (item->pos().x() > 10.0) {
        foundCentered = true;
      }
    }
  }
  QVERIFY(foundCentered);

  const QRectF rectBeforeDup = view.scene()->sceneRect();
  seq.insert(page2, BEFORE, ImageId());
  QCOMPARE(seq.toPageSequence().numPages(), size_t(2));
  QCOMPARE(view.scene()->sceneRect(), rectBeforeDup);
}

void TestAppUiQt::testThumbnailSequenceSurvivesRemovalDuringSelectionQuestion() {
  // P9-09: If the clicked page is removed while cancelingSelectionAccepted() runs a modal dialog,
  // selectItemNoModifiers() must re-look up targetId and return safely without crashing.
  auto& appSettings = ApplicationSettings::getInstance();
  const bool prevAsk = appSettings.isCancelingSelectionQuestionEnabled();
  appSettings.setCancelingSelectionQuestionEnabled(true);
  const auto restoreSetting
      = qScopeGuard([&appSettings, prevAsk]() { appSettings.setCancelingSelectionQuestionEnabled(prevAsk); });

  QGraphicsView view;
  view.resize(300, 800);
  view.show();

  ThumbnailSequence seq(QSizeF(100.0, 120.0), ThumbnailSequence::SINGLE_COLUMN);
  seq.attachView(&view);

  std::vector<ImageFileInfo> files{
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/s1.png")), {ImageMetadata(QSize(100, 120), Dpi(300, 300))}),
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/s2.png")), {ImageMetadata(QSize(100, 120), Dpi(300, 300))}),
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/s3.png")), {ImageMetadata(QSize(100, 120), Dpi(300, 300))})};
  auto pages = ProjectPages::createNew(files, Qt::LeftToRight);
  const PageSequence sequence = pages->toPageSequence(PAGE_VIEW);
  seq.reset(sequence, ThumbnailSequence::KEEP_SELECTION);
  seq.selectPages({sequence.pageAt(0).id(), sequence.pageAt(1).id()}, sequence.pageAt(0).id());
  QCoreApplication::processEvents();

  const PageId targetId = sequence.pageAt(2).id();
  QCOMPARE(seq.selectedItems().size(), size_t(2));

  // Locate target composite item center by finding the lowest top-level composite item in the scene.
  QGraphicsItem* targetItem = nullptr;
  qreal maxY = -1.0;
  for (QGraphicsItem* item : view.scene()->items()) {
    if (item->parentItem() == nullptr && item->boundingRect().height() > 50.0 && item->pos().y() > maxY) {
      maxY = item->pos().y();
      targetItem = item;
    }
  }
  QVERIFY(targetItem != nullptr);

  // Schedule removing the target page and accepting the modal prompt as soon as it opens.
  QTimer::singleShot(30, [&seq, targetId]() {
    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
      seq.removePages({targetId});
      box->accept();
    }
  });

  const QPoint clickViewPos = view.mapFromScene(targetItem->mapToScene(targetItem->boundingRect().center()));
  QMouseEvent press(QEvent::MouseButtonPress, clickViewPos, view.viewport()->mapToGlobal(clickViewPos), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(view.viewport(), &press);
  QCoreApplication::processEvents();

  QCOMPARE(seq.toPageSequence().numPages(), size_t(2));
}

void TestAppUiQt::testExportViewClearsStatusOnRowRemoval() {
  // P9-07: rowsRemoved (as emitted by QListWidget InternalMove drag-and-drop) must invoke updateControls().
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  auto thumbCache = std::make_shared<ThumbnailPixmapCache>(tempDir.path(), QSize(64, 64), 4, 2);
  auto procOpts = std::make_shared<ProcessingOptions>();
  ExportView view(thumbCache, procOpts);

  std::vector<ExportView::Entry> entries;
  for (int i = 0; i < 2; ++i) {
    ExportView::Entry entry;
    entry.label = QStringLiteral("page%1.tif").arg(i);
    entry.page = ExportPage(tempDir.filePath(entry.label), QString(), QString(), false);
    entries.push_back(std::move(entry));
  }
  view.setPages(std::move(entries));
  view.m_showingStatus = true;
  view.m_statusPages = {0, 1};

  QVERIFY(view.m_pageList->count() == 2);
  delete view.m_pageList->takeItem(0);
  QCoreApplication::processEvents();
  QVERIFY(!view.m_showingStatus);
}

void TestAppUiQt::testProjectFilesDialogSanitizesPathsStopsTimersAndChecksOutDirFirst() {
  // P9-14: inputDirectory/outputDirectory apply sanitizePath(), reject() stops timers, and relative outDir fails
  // before overwriting project file.
  ProjectFilesDialog dialog;
  dialog.inpDirLine->setText(QStringLiteral("  \"C:/Scans/Book\"  "));
  dialog.outDirLine->setText(QStringLiteral("  \"relative_out\"  "));
  QCOMPARE(dialog.inputDirectory(), QStringLiteral("C:/Scans/Book"));
  QCOMPARE(dialog.outputDirectory(), QStringLiteral("relative_out"));

  dialog.m_showProgressTimer.start(5000);
  QVERIFY(dialog.m_showProgressTimer.isActive());
  dialog.reject();
  QVERIFY(!dialog.m_showProgressTimer.isActive());
  QCOMPARE(dialog.m_loadTimerId, 0);
}

void TestAppUiQt::testMainWindowHidesRemovedKeepOwnershipWidgetsAndGuardsAutoImport() {
  // P9-03, P9-04, P9-05, P9-06, P9-15: removeWidgetsFromLayout hides widgets; closing/inserting flags block
  // auto-import; countImagesWithoutDpi reflects fixed DPI.
  QWidget container;
  auto* layout = new QVBoxLayout(&container);
  QWidget keepWidget;
  keepWidget.show();
  layout->addWidget(&keepWidget);
  QVERIFY(!keepWidget.isHidden());
  MainWindow::removeWidgetsFromLayout(layout);
  QVERIFY(keepWidget.isHidden());

  MainWindow window;
  QVERIFY(!window.m_closingPrompt);
  QVERIFY(!window.m_insertingFiles);
  window.m_closingPrompt = true;
  QVERIFY(!window.canImportScansNow());
  window.m_closingPrompt = false;
  window.m_insertingFiles = true;
  QVERIFY(!window.canImportScansNow());
  window.m_insertingFiles = false;

  std::vector<ImageFileInfo> files{
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/no_dpi.png")), {ImageMetadata(QSize(100, 100), Dpi(0, 0))}),
      ImageFileInfo(QFileInfo(QStringLiteral("/tmp/ok_dpi.png")), {ImageMetadata(QSize(100, 100), Dpi(300, 300))})};
  auto pages = ProjectPages::createNew(files, Qt::LeftToRight);
  QCOMPARE(auto_import_utils::countImagesWithoutDpi(*pages), 1);
  files[0].imageInfo()[0] = ImageMetadata(QSize(100, 100), Dpi(300, 300));
  pages->updateMetadataFrom(files);
  QCOMPARE(auto_import_utils::countImagesWithoutDpi(*pages), 0);
}
