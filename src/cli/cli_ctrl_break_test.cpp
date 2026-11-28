// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// An end-to-end check that Ctrl+Break cancels the command line tool like Ctrl+C on Windows. This is
// how another program, for example a script or a server driving the tool, stops it: it starts the tool
// in its own process group and sends CTRL_BREAK_EVENT, since Ctrl+C can't be sent to another group.
// The signal needs a real process, so this isn't a test in core_tests.

#include <windows.h>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtGlobal>

namespace {
int fail(const QString& message) {
  qCritical().noquote() << message;
  return 1;
}

/**
 * A page of A4 at 300 dpi with lines of "text", drawn here so the test needs no image file. Big enough that
 * the output of one page takes a while, so the run is still going when the signal comes.
 */
bool savePage(const QString& path, const int number) {
  QImage image(2480, 3508, QImage::Format_Mono);
  image.fill(Qt::white);
  QPainter painter(&image);
  for (int y = 300; y < 3200; y += 60) {
    for (int x = 250; x < 2200; x += 90 + ((x + y + number) % 50)) {
      painter.fillRect(x, y, 60, 30, Qt::black);
    }
  }
  painter.end();
  image.setDotsPerMeterX(11811);
  image.setDotsPerMeterY(11811);
  return image.save(path, "PNG");
}
}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  if (app.arguments().size() != 2) {
    return fail(QStringLiteral("Pass the path of scantailor-ocr-cli as the only argument."));
  }
  // The event goes to the processes of a console, so the test needs one to share with the tool. A test run
  // from a service may have none.
  if (GetConsoleCP() == 0) {
    AllocConsole();
  }

  QTemporaryDir dir;
  if (!dir.isValid()) {
    return fail(QStringLiteral("Unable to make a temporary folder."));
  }
  const QString inputDir = dir.filePath(QStringLiteral("in"));
  const int numPages = 20;
  if (!QDir().mkpath(inputDir)) {
    return fail(QStringLiteral("Unable to make the input folder."));
  }
  for (int i = 1; i <= numPages; ++i) {
    if (!savePage(inputDir + QStringLiteral("/%1.png").arg(i, 2, 10, QLatin1Char('0')), i)) {
      return fail(QStringLiteral("Unable to save the pages."));
    }
  }
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));
  const QString outputDir = inputDir + QStringLiteral("/out");

  QProcess tool;
  tool.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
    // QProcess starts the tool without a console when the console has no window, as in CI or a
    // pseudo console; then the event can't reach it.
    arguments->flags = (arguments->flags & ~DWORD(CREATE_NO_WINDOW)) | CREATE_NEW_PROCESS_GROUP;
  });
  // Nothing reads the progress while the test waits, so a full pipe could stall the tool.
  tool.setStandardOutputFile(QProcess::nullDevice());
  // One thread, so the run takes long enough; the built-in profile, so it doesn't depend on the machine.
  tool.start(app.arguments().at(1), {QStringLiteral("--profile=Default"), QStringLiteral("--threads=1"),
                                     QStringLiteral("--report-file=") + reportFile, inputDir});
  if (!tool.waitForStarted(30000)) {
    return fail(QStringLiteral("The command line tool didn't start."));
  }

  // Waiting for the first output file means the run is in its last step, with pages done and pages left.
  const QDeadlineTimer firstOutput(120000);
  while (QDir(outputDir).entryList({QStringLiteral("*.tif")}, QDir::Files).isEmpty()) {
    if (firstOutput.hasExpired() || (tool.state() == QProcess::NotRunning)) {
      return fail(QStringLiteral("The tool wrote no output file before the signal."));
    }
    QThread::msleep(20);
  }
  if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(tool.processId()))) {
    return fail(QStringLiteral("Unable to send Ctrl+Break: error %1").arg(GetLastError()));
  }
  // A second immediate break must also set the flag rather than killing the process via SIG_DFL.
  QThread::msleep(10);
  (void) GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, static_cast<DWORD>(tool.processId()));
  if (!tool.waitForFinished(120000)) {
    return fail(QStringLiteral("The tool didn't stop after Ctrl+Break."));
  }

  // Ended by the system instead of cancelling, the tool exits with STATUS_CONTROL_C_EXIT, which QProcess
  // reports as a crash.
  if (tool.exitStatus() != QProcess::NormalExit) {
    return fail(QStringLiteral("The tool was ended at once instead of cancelling."));
  }
  if (tool.exitCode() != 5) {
    return fail(QStringLiteral("The tool exited with %1 instead of 5 (cancelled): %2")
                    .arg(tool.exitCode())
                    .arg(QString::fromLocal8Bit(tool.readAllStandardError())));
  }
  if (!QFile::exists(inputDir + QStringLiteral("/in.ScanTailor"))) {
    return fail(QStringLiteral("The project file wasn't saved."));
  }
  QFile report(reportFile);
  if (!report.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("The report file wasn't written."));
  }
  const QJsonObject run = QJsonDocument::fromJson(report.readAll()).object()[QStringLiteral("run")].toObject();
  if (!run[QStringLiteral("canceled")].toBool()) {
    return fail(QStringLiteral("The report doesn't say the run was canceled."));
  }
  if (QDir(outputDir).entryList({QStringLiteral("*.tif")}, QDir::Files).size() >= numPages) {
    return fail(QStringLiteral("The run finished all pages, so the signal came too late to test anything."));
  }
  return 0;
}
