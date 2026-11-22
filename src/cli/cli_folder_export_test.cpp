// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// An end-to-end check of one command-line rule: an export option without --last-step runs to the
// export. The other tests of what the tool does live in core_tests; this one needs the real tool
// because the mapping from options to the last step is in main.cpp.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtGlobal>

namespace {
int fail(const QString& message) {
  qCritical().noquote() << message;
  return 1;
}

/** A small scanned page with 300 dpi, drawn here so the test needs no image file. */
bool savePage(const QString& path) {
  QImage image(100, 100, QImage::Format_Mono);
  image.fill(Qt::white);
  QPainter painter(&image);
  painter.fillRect(15, 15, 70, 70, Qt::black);
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

  QTemporaryDir dir;
  if (!dir.isValid()) {
    return fail(QStringLiteral("Unable to make a temporary folder."));
  }
  const QString inputDir = dir.filePath(QStringLiteral("in"));
  if (!QDir().mkpath(inputDir) || !savePage(inputDir + QStringLiteral("/page.png"))) {
    return fail(QStringLiteral("Unable to make the input folder or its page."));
  }
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));

  QProcess tool;
  tool.start(app.arguments().at(1),
             // The built-in profile, so the run doesn't depend on the profile of the machine's settings.
             {QStringLiteral("--profile=Default"), QStringLiteral("--export-formats=pdf"),
              QStringLiteral("--report-file=") + reportFile, inputDir});
  if (!tool.waitForStarted(30000) || !tool.waitForFinished(120000)) {
    return fail(QStringLiteral("The command line tool didn't finish."));
  }
  if ((tool.exitStatus() != QProcess::NormalExit) || (tool.exitCode() != 0)) {
    return fail(QStringLiteral("The tool exited with %1: %2")
                    .arg(tool.exitCode())
                    .arg(QString::fromLocal8Bit(tool.readAllStandardError())));
  }

  QFile report(reportFile);
  if (!report.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("The report file wasn't written."));
  }
  const QJsonObject root = QJsonDocument::fromJson(report.readAll()).object();
  const QJsonObject run = root[QStringLiteral("run")].toObject();
  if (run[QStringLiteral("lastStep")].toString() != QStringLiteral("export")) {
    return fail(
        QStringLiteral("The run stopped at %1 instead of export.").arg(run[QStringLiteral("lastStep")].toString()));
  }
  const QJsonArray exports = root[QStringLiteral("exports")].toArray();
  if ((exports.size() != 1) || !QFile::exists(exports.at(0).toString())) {
    return fail(QStringLiteral("The report doesn't name the exported PDF."));
  }
  return 0;
}
