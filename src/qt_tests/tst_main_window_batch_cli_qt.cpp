// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_main_window_batch_cli_qt.h"

#include <ApplicationSettings.h>
#include <CommandLineRun.h>
#include <DefaultParams.h>
#include <MainWindow.h>
#include <PageSequence.h>
#include <ProjectPages.h>
#include <StageListView.h>
#include <StageSequence.h>

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <vector>

#include "core/tests/ScopedMaxThreads.h"
#include "test_project.h"

using namespace test_pages;

namespace {

class SettingsGuard {
 public:
  SettingsGuard() : m_settings(ApplicationSettings::getInstance()) {
    m_openGl = m_settings.isOpenGlEnabled();
    m_autoSave = m_settings.isAutoSaveProjectEnabled();
  }

  ~SettingsGuard() {
    m_settings.setOpenGlEnabled(m_openGl);
    m_settings.setAutoSaveProjectEnabled(m_autoSave);
  }

  SettingsGuard(const SettingsGuard&) = delete;
  SettingsGuard& operator=(const SettingsGuard&) = delete;

 private:
  ApplicationSettings& m_settings;
  bool m_openGl = false;
  bool m_autoSave = false;
};

QStringList outputFiles(const QString& dir) {
  QStringList files = QDir(dir).entryList(QStringList() << "*.tif" << "*.tiff", QDir::Files, QDir::Name);
  for (QString& f : files) {
    f = QDir(dir).absoluteFilePath(f);
  }
  return files;
}

QByteArray readFileBytes(const QString& path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }
  return f.readAll();
}

class ModalDialogWatchdog : public QObject {
 public:
  explicit ModalDialogWatchdog(QObject* parent = nullptr) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &ModalDialogWatchdog::check);
    m_timer.start(100);
  }

  QString message() const { return m_message; }

 private:
  void check() {
    QWidget* modal = QApplication::activeModalWidget();
    if (!modal || (modal == m_lastSeen)) {
      return;
    }
    m_lastSeen = modal;
    m_message = QStringLiteral("Unexpected modal dialog: %1 (%2)")
                    .arg(QString::fromLatin1(modal->metaObject()->className()), modal->windowTitle());
    qWarning().noquote() << m_message;
    // A modal dialog blocks the test thread's event loop; close it so the test can
    // fail with a message instead of hanging until CI kills the job.
    modal->close();
  }

  QTimer m_timer;
  QPointer<QWidget> m_lastSeen;
  QString m_message;
};

void waitForProjectLoaded(MainWindow& mw, ModalDialogWatchdog& watchdog) {
  QTRY_VERIFY_WITH_TIMEOUT(mw.allPages().numPages() > 0, 60000);
  QVERIFY2(watchdog.message().isEmpty(), qPrintable(watchdog.message()));
}

void waitForBatchFinished(StageListView* filterList, ModalDialogWatchdog& watchdog) {
  QTRY_VERIFY_WITH_TIMEOUT(filterList->isEnabled(), 180000);
  QVERIFY2(watchdog.message().isEmpty(), qPrintable(watchdog.message()));
}

void runGuiBatch(MainWindow& mw, const QString& projectFile) {
  ModalDialogWatchdog watchdog(&mw);

  mw.openProject(projectFile);
  waitForProjectLoaded(mw, watchdog);

  auto* filterList = mw.findChild<StageListView*>(QStringLiteral("filterList"));
  QVERIFY2(filterList != nullptr, "MainWindow filterList not found");

  // StageSequence defines the filter order used by the app: ... page_layout, output.
  // Using StageSequence here avoids hard-coded row numbers and keeps the test tied to
  // the same public source of truth as the app, without accessing MainWindow internals.
  const StageSequence stages(std::make_shared<ProjectPages>());
  const int marginsRow = stages.pageLayoutFilterIdx();
  const int outputRow = stages.outputFilterIdx();

  filterList->selectRow(marginsRow);
  QTRY_COMPARE_WITH_TIMEOUT(filterList->currentIndex().row(), marginsRow, 10000);
  QVERIFY2(watchdog.message().isEmpty(), qPrintable(watchdog.message()));

  QVERIFY(QMetaObject::invokeMethod(&mw, "startBatchProcessing", Qt::DirectConnection));
  // MainWindow::startBatchProcessingOf() disables the filter list before it hands out the
  // tasks. The results arrive in the GUI thread, and MainWindow::filterResult() enables the
  // list again through stopBatchProcessing() only once all pages are processed, however many
  // run at the same time.
  QVERIFY2(!filterList->isEnabled(), "Batch processing did not start");
  waitForBatchFinished(filterList, watchdog);

  filterList->selectRow(outputRow);
  QTRY_COMPARE_WITH_TIMEOUT(filterList->currentIndex().row(), outputRow, 10000);
  QVERIFY2(watchdog.message().isEmpty(), qPrintable(watchdog.message()));

  QVERIFY(QMetaObject::invokeMethod(&mw, "startBatchProcessing", Qt::DirectConnection));
  QVERIFY2(!filterList->isEnabled(), "Batch processing did not start");
  waitForBatchFinished(filterList, watchdog);
}

QStringList createProject(const QDir& dir, const DefaultParams& startValues) {
  QDir inDir(dir.filePath(QStringLiteral("in")));
  QDir outDir(dir.filePath(QStringLiteral("out")));
  if (!inDir.mkpath(QStringLiteral(".")) || !outDir.mkpath(QStringLiteral("."))) {
    return {};
  }

  const std::vector<ImageFileInfo> files = saveScans(inDir);
  if (!allSaved(files)) {
    return {};
  }

  TestProject project(files, outDir.absolutePath(), startValues);
  const QString projectFile = dir.absoluteFilePath(QStringLiteral("project.ScanTailor"));
  if (!project.write(projectFile)) {
    return {};
  }
  return {projectFile, outDir.absolutePath()};
}

}  // namespace

void TestMainWindowBatchCliQt::testGuiBatchMatchesCli() {
  const SettingsGuard settingsGuard;
  ApplicationSettings::getInstance().setOpenGlEnabled(false);
  ApplicationSettings::getInstance().setAutoSaveProjectEnabled(false);

  QTemporaryDir tmp;
  QVERIFY(tmp.isValid());

  const DefaultParams startValues;

  QDir guiDir(tmp.filePath(QStringLiteral("gui")));
  QVERIFY(guiDir.mkpath(QStringLiteral(".")));
  const QStringList guiPaths = createProject(guiDir, startValues);
  QCOMPARE(guiPaths.size(), 2);
  const QString guiProjectFile = guiPaths.at(0);
  const QString guiOutDir = guiPaths.at(1);

  QElapsedTimer guiTimer;
  guiTimer.start();
  {
    // The app path processes pages one at a time in the GUI batch.
    const Tests::ScopedMaxThreads scopedThreads(1);
    MainWindow mw;
    runGuiBatch(mw, guiProjectFile);
  }
  const qint64 guiMs = guiTimer.elapsed();

  QDir cliDir(tmp.filePath(QStringLiteral("cli")));
  QVERIFY(cliDir.mkpath(QStringLiteral(".")));
  const QStringList cliPaths = createProject(cliDir, startValues);
  QCOMPARE(cliPaths.size(), 2);
  const QString cliProjectFile = cliPaths.at(0);
  const QString cliOutDir = cliPaths.at(1);

  QElapsedTimer cliTimer;
  cliTimer.start();
  int cliResult = CommandLineRun::PAGES_FAILED;
  {
    // The command line tool processes pages in parallel; compare it against the GUI
    // path to make sure both produce the same output.
    const Tests::ScopedMaxThreads scopedThreads(4);
    QString outText;
    QString errText;
    QTextStream out(&outText);
    QTextStream err(&errText);
    const std::atomic<bool> cancel(false);
    CommandLineRun::Options options;
    options.projectFile = cliProjectFile;
    cliResult = CommandLineRun::run(options, out, err, cancel);
    if (cliResult != CommandLineRun::SUCCESS) {
      qWarning().noquote() << "CLI stdout:" << outText;
      qWarning().noquote() << "CLI stderr:" << errText;
    }
  }
  const qint64 cliMs = cliTimer.elapsed();
  QCOMPARE(cliResult, static_cast<int>(CommandLineRun::SUCCESS));

  const QStringList guiFiles = outputFiles(guiOutDir);
  const QStringList cliFiles = outputFiles(cliOutDir);
  // A skewed page, a spread split into two pages and a color page.
  QCOMPARE(guiFiles.size(), 4);
  QCOMPARE(cliFiles.size(), 4);

  QStringList guiNames;
  QStringList cliNames;
  for (const QString& f : guiFiles) {
    guiNames.push_back(QFileInfo(f).fileName());
  }
  for (const QString& f : cliFiles) {
    cliNames.push_back(QFileInfo(f).fileName());
  }
  QCOMPARE(guiNames, cliNames);

  for (int i = 0; i < guiFiles.size(); ++i) {
    const QByteArray guiBytes = readFileBytes(guiFiles[i]);
    const QByteArray cliBytes = readFileBytes(cliFiles[i]);
    if (guiBytes != cliBytes) {
      qWarning() << "Output differs:" << guiNames.at(i) << "gui bytes" << guiBytes.size() << "cli bytes"
                 << cliBytes.size();
    }
    QCOMPARE(guiBytes, cliBytes);
  }

  qInfo() << "GUI batch ms:" << guiMs << "CLI run ms:" << cliMs << "files:" << guiFiles.size();
}
