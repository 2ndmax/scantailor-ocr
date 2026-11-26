// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The command line tool: processes a project as the app's batch processing does, without a window.
// What it does is in CommandLineRun; this only reads the command line and sets up the program as
// the app does.

#include <ApplicationSettings.h>
#include <CommandLineRun.h>
#include <CoreUtils.h>
#include <ImageSizeLimit.h>
#include <OcrEngine.h>
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
#else
#include <signal.h>
#endif

namespace {
std::atomic<bool> cancelRequested(false);
static_assert(std::atomic<bool>::is_always_lock_free);

#ifdef _WIN32
BOOL WINAPI consoleCtrlHandler(const DWORD ctrlType) {
  switch (ctrlType) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
      cancelRequested.store(true);
      return TRUE;
    default:
      return FALSE;
  }
}
#else
void requestCancel(const int /*signalNumber*/) {
  cancelRequested.store(true);
}
#endif

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

/**
 * The DPI of a --dpi/--dpi-fix value: a number, or horizontal and vertical values like 600x300.
 */
bool parseDpiValue(const QString& text, Dpi* dpi) {
  const QStringList parts = text.trimmed().toLower().split(QLatin1Char('x'), Qt::SkipEmptyParts);
  if (parts.isEmpty() || (parts.size() > 2)) {
    return false;
  }
  bool ok = false;
  const int horizontal = core::CoreUtils::dpiFromText(parts.at(0), &ok);
  if (!ok || (horizontal <= 0)) {
    return false;
  }
  int vertical = horizontal;
  if (parts.size() == 2) {
    vertical = core::CoreUtils::dpiFromText(parts.at(1), &ok);
    if (!ok || (vertical <= 0)) {
      return false;
    }
  }
  *dpi = Dpi(horizontal, vertical);
  return true;
}
}  // namespace

int main(int argc, char* argv[]) {
  OcrEngine::limitThreads();
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
      CommandLineRun::tr("Processes the pages of a ScanTailor OCR project up to the last step --last-step "
                         "names, at most Output, and saves the project. With a folder as the argument, the tool "
                         "creates the project first."));
  const QCommandLineOption helpOption = parser.addHelpOption();
  const QCommandLineOption versionOption = parser.addVersionOption();
  const QCommandLineOption profileOption(
      QStringLiteral("profile"),
      CommandLineRun::tr("The profile of the new project: its name, or the file of one (a .stp file); without "
                         "it, the profile of the program is used."),
      CommandLineRun::tr("name or file"));
  parser.addOption(profileOption);
  const QCommandLineOption dpiOption(
      QStringLiteral("dpi"),
      CommandLineRun::tr("The DPI of all images of the new project: a number, or horizontal and vertical "
                         "values like 600x300."),
      CommandLineRun::tr("n or HxV"));
  parser.addOption(dpiOption);
  const QCommandLineOption dpiFixOption(
      QStringLiteral("dpi-fix"),
      CommandLineRun::tr("The DPI of the images that need fixing (red and yellow) in the new project: a "
                         "number, or horizontal and vertical values like 600x300."),
      CommandLineRun::tr("n or HxV"));
  parser.addOption(dpiFixOption);
  const QCommandLineOption projectFileOption(
      QStringLiteral("project-file"),
      CommandLineRun::tr("The file of the new project instead of the default of the input folder; its folder "
                         "must exist."),
      CommandLineRun::tr("file"));
  parser.addOption(projectFileOption);
  const QCommandLineOption outputFolderOption(
      QStringLiteral("output-folder"),
      CommandLineRun::tr("The folder the output of the new project goes to instead of <input>/out; it is "
                         "created when it doesn't exist."),
      CommandLineRun::tr("folder"));
  parser.addOption(outputFolderOption);
  const QCommandLineOption lastStepOption(
      QStringLiteral("last-step"),
      CommandLineRun::tr("The last step of the run: fix-orientation, split-pages, deskew, select-content, "
                         "margins, output or export. The export runs after output. Default: output."),
      CommandLineRun::tr("step"));
  parser.addOption(lastStepOption);
  const QCommandLineOption colorModeDetectOption(
      QStringLiteral("color-mode-detect"),
      CommandLineRun::tr("Detect the color mode of the pages after the Margins step, as the app's Detect "
                         "Color Mode does, and report it per page."));
  parser.addOption(colorModeDetectOption);
  const QCommandLineOption orientationDetectOption(
      QStringLiteral("orientation-detect"),
      CommandLineRun::tr("Detect the orientation of the pages in the folder mode, before the first step: "
                         "pages rotates the pages whose orientation is certain; book also rotates uncertain pages like "
                         "their neighbors or the majority."),
      CommandLineRun::tr("method"));
  parser.addOption(orientationDetectOption);
  const QCommandLineOption folderOption(
      QStringLiteral("export-folder"),
      CommandLineRun::tr("The folder for the exported files instead of the folder of the project file; it is "
                         "created when it doesn't exist."),
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
  const QCommandLineOption threadsOption(
      QStringLiteral("threads"),
      CommandLineRun::tr("The number of threads of the run, instead of the setting of the program; at least 1."),
      CommandLineRun::tr("n"));
  parser.addOption(threadsOption);
  const QCommandLineOption reportFileOption(
      QStringLiteral("report-file"),
      CommandLineRun::tr("The JSON report of the run, written at the end: the mode, the project file, the "
                         "profile and the last step, the exit code and cancelled, the pages with their DPI, "
                         "color mode, output file and status, and the exports and the files passed over."),
      CommandLineRun::tr("file"));
  parser.addOption(reportFileOption);
  const QCommandLineOption progressJsonOption(
      QStringLiteral("progress-json"),
      CommandLineRun::tr("The progress as JSON lines on the standard output, one per event; all other text then "
                         "goes to the standard error."));
  const QCommandLineOption profileListOption(
      QStringLiteral("profile-list"),
      CommandLineRun::tr("The profiles --profile takes, as JSON on the standard output; also names the profile a "
                         "run without --profile uses."));
  const QCommandLineOption projectInfoOption(
      QStringLiteral("project-info"),
      CommandLineRun::tr("The pages of the project file, with their file, DPI, color mode and output file, as JSON "
                         "on the standard output. Needs a project file."));
  parser.addOption(progressJsonOption);
  parser.addOption(profileListOption);
  parser.addOption(projectInfoOption);
  parser.addPositionalArgument(CommandLineRun::tr("folder or project file"),
                               CommandLineRun::tr("The input folder for a new project or an existing project file."));
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
  const bool profileListMode = parser.isSet(profileListOption);
  const bool projectInfoMode = parser.isSet(projectInfoOption);

  if (profileListMode || projectInfoMode) {
    if (profileListMode && projectInfoMode) {
      err << CommandLineRun::tr("--profile-list and --project-info can't be used together.") << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    const QString modeOption = profileListMode ? QStringLiteral("profile-list") : QStringLiteral("project-info");
    for (const QString& name : parser.optionNames()) {
      if (name != modeOption) {
        err << CommandLineRun::tr("The option --%1 can't be used with --%2.").arg(name, modeOption) << Qt::endl;
        return CommandLineRun::USAGE_ERROR;
      }
    }
    if (profileListMode) {
      if (!parser.positionalArguments().isEmpty()) {
        err << CommandLineRun::tr("--profile-list takes no argument.") << Qt::endl;
        return CommandLineRun::USAGE_ERROR;
      }
      return CommandLineRun::profileList(out, err);
    }
    if (parser.positionalArguments().size() != 1) {
      err << CommandLineRun::tr("--project-info needs a project file.") << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    const QString projectInfoArg = parser.positionalArguments().at(0);
    if (!projectInfoArg.endsWith(QStringLiteral(".ScanTailor"), Qt::CaseInsensitive)) {
      err << CommandLineRun::tr("--project-info needs a project file.") << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    return CommandLineRun::projectInfo(projectInfoArg, out, err);
  }

  if (parser.positionalArguments().size() != 1) {
    err << parser.helpText();
    return CommandLineRun::USAGE_ERROR;
  }

  // Ctrl+C stops before the next page, so no output file is left half-written.
#ifdef _WIN32
  SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);
#else
  struct sigaction sa{};
  sa.sa_handler = requestCancel;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
#endif

  const auto requireNonEmptyValue = [&parser, &err](const QCommandLineOption& option, QString* value) {
    if (!parser.isSet(option)) {
      return true;
    }
    const QString parsed = parser.value(option);
    if (parsed.trimmed().isEmpty()) {
      err << CommandLineRun::tr("The option --%1 needs a value.").arg(option.names().first()) << Qt::endl;
      return false;
    }
    if (value) {
      *value = parsed;
    }
    return true;
  };

  CommandLineRun::Options options;
  const QString argument = parser.positionalArguments().at(0);
  if (argument.endsWith(QStringLiteral(".ScanTailor"), Qt::CaseInsensitive)) {
    options.projectFile = argument;
  } else {
    options.inputFolder = argument;
  }
  // The values are read in both modes; run() decides which of them are allowed where. That keeps
  // the checks of the command line in one place and reports an unusable combination as a usage error.
  if (parser.isSet(projectFileOption)) {
    if (options.inputFolder.isEmpty()) {
      err << CommandLineRun::tr(
          "The project file is the argument of the call; --project-file is for the "
          "folder mode.")
          << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    if (!requireNonEmptyValue(projectFileOption, &options.projectFile)) {
      return CommandLineRun::USAGE_ERROR;
    }
  }
  if (parser.isSet(outputFolderOption)) {
    QString outputFolder;
    if (!requireNonEmptyValue(outputFolderOption, &outputFolder)) {
      return CommandLineRun::USAGE_ERROR;
    }
    options.outputFolder = outputFolder;
  }
  if (!requireNonEmptyValue(profileOption, &options.profile)) {
    return CommandLineRun::USAGE_ERROR;
  }
  if (parser.isSet(dpiOption)) {
    Dpi dpi;
    if (!parseDpiValue(parser.value(dpiOption), &dpi)) {
      err << CommandLineRun::tr(
                 "Unknown DPI after --dpi: %1; pass a number or horizontal and vertical "
                 "values like 600x300.")
                 .arg(parser.value(dpiOption))
          << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    options.dpi = dpi;
  }
  if (parser.isSet(dpiFixOption)) {
    Dpi dpi;
    if (!parseDpiValue(parser.value(dpiFixOption), &dpi)) {
      err << CommandLineRun::tr(
                 "Unknown DPI after --dpi-fix: %1; pass a number or horizontal and vertical "
                 "values like 600x300.")
                 .arg(parser.value(dpiFixOption))
          << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    options.dpiFix = dpi;
  }
  options.colorModeDetect = parser.isSet(colorModeDetectOption);
  if (!requireNonEmptyValue(orientationDetectOption, &options.orientationDetect)) {
    return CommandLineRun::USAGE_ERROR;
  }
  if (parser.isSet(threadsOption)) {
    bool ok = false;
    const int threads = parser.value(threadsOption).toInt(&ok);
    if (!ok || (threads < 1)) {
      err << CommandLineRun::tr("Unknown thread number after --threads: %1; it must be at least 1.")
                 .arg(parser.value(threadsOption))
          << Qt::endl;
      return CommandLineRun::USAGE_ERROR;
    }
    options.threads = threads;
  }
  if (parser.isSet(reportFileOption)) {
    QString reportFile;
    if (!requireNonEmptyValue(reportFileOption, &reportFile)) {
      return CommandLineRun::USAGE_ERROR;
    }
    options.reportFile = reportFile;
  }
  const bool lastStepSet = parser.isSet(lastStepOption);
  if (lastStepSet && !CommandLineRun::Options::parseLastStep(parser.value(lastStepOption), &options.lastStep)) {
    err << CommandLineRun::tr(
               "Unknown step after --last-step: %1. The steps are: fix-orientation, split-pages, "
               "deskew, select-content, margins, output and export.")
               .arg(parser.value(lastStepOption))
        << Qt::endl;
    return CommandLineRun::USAGE_ERROR;
  }
  // The export options lead the run to the export; they need --last-step export, or no --last-step.
  const bool exportOptions = parser.isSet(folderOption) || parser.isSet(nameOption) || parser.isSet(formatsOption);
  if (exportOptions && !lastStepSet) {
    options.lastStep = CommandLineRun::Options::EXPORT;
  }
  if (parser.isSet(folderOption)) {
    QString exportDir;
    if (!requireNonEmptyValue(folderOption, &exportDir)) {
      return CommandLineRun::USAGE_ERROR;
    }
    options.exportDir = exportDir;
  }
  if (parser.isSet(nameOption)) {
    QString exportName;
    if (!requireNonEmptyValue(nameOption, &exportName)) {
      return CommandLineRun::USAGE_ERROR;
    }
    options.exportName = exportName;
  }
  if (parser.isSet(formatsOption)) {
    QString formats;
    if (!requireNonEmptyValue(formatsOption, &formats)) {
      return CommandLineRun::USAGE_ERROR;
    }
    options.formats = formats;
  }
  if (parser.isSet(progressJsonOption)) {
    return CommandLineRun::run(options, err, err, cancelRequested, &out);
  }
  return CommandLineRun::run(options, out, err, cancelRequested);
}
