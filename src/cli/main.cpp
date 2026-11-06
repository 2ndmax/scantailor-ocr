// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The command line tool: processes a project as the app's batch processing does, without a window.
// What it does is in CommandLineRun; this only reads the command line and sets up the program as
// the app does.

#include <ApplicationSettings.h>
#include <CommandLineRun.h>
#include <ImageSizeLimit.h>
#include <SettingsSetup.h>
#include <Translations.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>
#include <atomic>
#include <csignal>

#include "version.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {
std::atomic<bool> cancelRequested(false);

void requestCancel(const int signalNumber) {
  cancelRequested = true;
  // Windows (and other systems) reset the handler before calling it. Without this, a second Ctrl+C
  // would end the tool at once, leaving temporary files and the changed code page behind.
  std::signal(signalNumber, requestCancel);
}

#ifdef _WIN32
/**
 * Switches the console to UTF-8 while the tool runs. The code page belongs to the console window,
 * not to the program, so older programs started there later would show umlauts wrong without
 * switching back.
 */
class Utf8Console {
 public:
  Utf8Console() : m_previousCodePage(GetConsoleOutputCP()) { SetConsoleOutputCP(CP_UTF8); }

  ~Utf8Console() {
    // Zero means there was no console to switch.
    if (m_previousCodePage != 0) {
      SetConsoleOutputCP(m_previousCodePage);
    }
  }

  Utf8Console(const Utf8Console&) = delete;
  Utf8Console& operator=(const Utf8Console&) = delete;

 private:
  const UINT m_previousCodePage;
};
#endif
}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  ImageSizeLimit::applyQtAllocationLimit();
#ifdef _WIN32
  // As in the app: the Qt plugins are next to the program, not in Qt's installation directory.
  QCoreApplication::setLibraryPaths(QStringList(QCoreApplication::applicationDirPath()));
  // The messages are written as UTF-8, which the console shows correctly only with this. Declared
  // before the streams, so they are flushed before the console is switched back.
  const Utf8Console utf8Console;
#endif
  QCoreApplication::setApplicationVersion(VERSION);

  // The settings and the language of the app.
  settings_setup::setUp();
  Translations translations;
  translations.install(ApplicationSettings::getInstance().getLanguage());

  QTextStream out(stdout);
  QTextStream err(stderr);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      CommandLineRun::tr("Processes all pages of a ScanTailor OCR project through all six steps and saves the "
                         "project. Set up the project in ScanTailor OCR first."));
  const QCommandLineOption helpOption = parser.addHelpOption();
  const QCommandLineOption versionOption = parser.addVersionOption();
  const QCommandLineOption exportOption(
      QStringLiteral("export"),
      CommandLineRun::tr("Then exports all pages with the settings of the project, next to the project file and "
                         "named like it, e.g. Book.pdf, Book.txt, Book.hocr and the folder Book_alto. Each "
                         "--export-... option turns on the export as well and changes what it names."));
  parser.addOption(exportOption);
  const QCommandLineOption folderOption(
      QStringLiteral("export-folder"),
      CommandLineRun::tr("The existing folder for the exported files instead of the folder of the project file."),
      CommandLineRun::tr("folder"));
  parser.addOption(folderOption);
  const QCommandLineOption nameOption(
      QStringLiteral("export-name"),
      CommandLineRun::tr("The name of the exported files, without extension, instead of the name of the project file."),
      CommandLineRun::tr("name"));
  parser.addOption(nameOption);
  const QCommandLineOption formatsOption(
      QStringLiteral("export-formats"),
      CommandLineRun::tr("The formats to export instead of those ticked in the project: pdf, txt, hocr and alto, "
                         "separated by commas or plus signs."),
      CommandLineRun::tr("list"));
  parser.addOption(formatsOption);
  parser.addPositionalArgument(CommandLineRun::tr("project"), CommandLineRun::tr("The project file."));
  // Not QCommandLineParser::process(): on Windows, without a console, it shows help and errors in a
  // message box, while a script expects them in its output.
  if (!parser.parse(QCoreApplication::arguments())) {
    err << parser.errorText() << Qt::endl << Qt::endl << parser.helpText();
    return CommandLineRun::USAGE_ERROR;
  }
  if (parser.isSet(helpOption)) {
    out << parser.helpText();
    return CommandLineRun::SUCCESS;
  }
  if (parser.isSet(versionOption)) {
    out << QCoreApplication::applicationName() << ' ' << QCoreApplication::applicationVersion() << Qt::endl;
    return CommandLineRun::SUCCESS;
  }
  if (parser.positionalArguments().size() != 1) {
    err << parser.helpText();
    return CommandLineRun::USAGE_ERROR;
  }

  // Ctrl+C stops before the next page, so no output file is left half-written.
  std::signal(SIGINT, requestCancel);
  std::signal(SIGTERM, requestCancel);

  CommandLineRun::Options options;
  options.projectFile = parser.positionalArguments().at(0);
  options.exportRequested = parser.isSet(exportOption);
  if (parser.isSet(folderOption)) {
    options.exportDir = parser.value(folderOption);
  }
  if (parser.isSet(nameOption)) {
    options.exportName = parser.value(nameOption);
  }
  if (parser.isSet(formatsOption)) {
    options.formats = parser.value(formatsOption);
  }
  return CommandLineRun::run(options, out, err, cancelRequested);
}
