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

void requestCancel(int) {
  cancelRequested = true;
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
  const QCommandLineOption pdfOption(
      QStringLiteral("pdf"),
      CommandLineRun::tr("Then creates the PDF <file> from all pages, with the PDF settings of the project, "
                         "including the text recognition."),
      CommandLineRun::tr("file"));
  parser.addOption(pdfOption);
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
  // E.g. from an empty variable in a script, which expects a PDF.
  if (parser.isSet(pdfOption) && parser.value(pdfOption).isEmpty()) {
    err << CommandLineRun::tr("The file name after --pdf is empty.") << Qt::endl;
    return CommandLineRun::USAGE_ERROR;
  }

  // Ctrl+C stops before the next page, so no output file is left half-written.
  std::signal(SIGINT, requestCancel);
  std::signal(SIGTERM, requestCancel);

  CommandLineRun::Options options;
  options.projectFile = parser.positionalArguments().at(0);
  options.pdfFile = parser.value(pdfOption);
  return CommandLineRun::run(options, out, err, cancelRequested);
}
