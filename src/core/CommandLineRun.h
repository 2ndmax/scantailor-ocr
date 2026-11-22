// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COMMANDLINERUN_H_
#define SCANTAILOR_CORE_COMMANDLINERUN_H_

#include <QCoreApplication>
#include <QString>
#include <atomic>
#include <optional>

#include "Dpi.h"
#include "ProcessingOptions.h"

class QTextStream;

/**
 * What the command line tool does: it opens a project, processes its pages up to the last step
 * --last-step names, at most Output, and saves the project. With --last-step export it then
 * exports the pages in the chosen formats, as the Export step of the app does. It runs in the
 * calling thread and needs only a QCoreApplication, so it can be tested without the tool itself.
 */
class CommandLineRun {
  Q_DECLARE_TR_FUNCTIONS(CommandLineRun)

 public:
  /** The exit codes of the tool. */
  enum ExitCode {
    SUCCESS = 0,
    /** The command line was wrong. */
    USAGE_ERROR = 1,
    /** The project couldn't be read, can't be processed as it is, or couldn't be saved. */
    PROJECT_UNUSABLE = 2,
    /** Some pages couldn't be processed or output. */
    PAGES_FAILED = 3,
    /** The export failed. */
    EXPORT_FAILED = 4,
    /** Processing was cancelled. */
    CANCELLED = 5
  };

  struct Options {
    /** The last step of a run, as the names of --last-step. */
    enum LastStep {
      FIX_ORIENTATION,
      SPLIT_PAGES,
      DESKEW,
      SELECT_CONTENT,
      MARGINS,
      OUTPUT,
      /** The export runs after the Output step, with the settings of the project. */
      EXPORT
    };

    QString projectFile;

    /**
     * The last step of the run; the export runs after the Output step when it is EXPORT.
     */
    LastStep lastStep = OUTPUT;

    /**
     * The folder of the new project: the argument of the call in folder mode, empty in project
     * mode. --project-file gives the project file then.
     */
    QString inputFolder;
    /** The folder the output of the new project goes to; set by --output-folder; empty for the default. */
    std::optional<QString> outputFolder;
    /** --profile: the name or the file of a profile; empty for the profile of the program. */
    QString profile;
    /** --dpi: the DPI of all images of the new project; unset without it. */
    std::optional<Dpi> dpi;
    /** --dpi-fix: the DPI of the images that need fixing; unset without it. */
    std::optional<Dpi> dpiFix;
    /** --color-mode-detect: after the Margins step, as the app's Detect Color Mode does. */
    bool colorModeDetect = false;
    /** --threads: the number of threads of this run; unset without it. */
    std::optional<int> threads;

    /**
     * The existing folder for the exported files. Not set for the folder of the project file; a set
     * but empty value is an error, e.g. from an empty variable in a script.
     */
    std::optional<QString> exportDir;
    /** The name of the exported files, see ExportPaths::fromBase(). Not set for the name of the project file. */
    std::optional<QString> exportName;
    /** The formats as for parseFormats(). Not set for the formats of the project. */
    std::optional<QString> formats;

    /** Whether the pages are exported after processing, as for lastStep == EXPORT. */
    bool exports() const { return lastStep == EXPORT; }

    /** The name of the step as --last-step takes it, e.g. for the help text. */
    static QString stepName(LastStep step);

    /** Parses the value of --last-step; false for an unknown name. */
    static bool parseLastStep(const QString& name, LastStep* step);
  };

  /**
   * Runs the tool. The progress goes to \p out, errors go to \p err. Setting \p cancel stops
   * processing before the next page; the pages done so far keep their results, and the project
   * is saved with them. A run that stops before Output doesn't export, and is a success when no
   * page failed and the run reached its last step.
   */
  static ExitCode run(const Options& options, QTextStream& out, QTextStream& err, const std::atomic<bool>& cancel);

  /**
   * Reads a list of the formats pdf, txt, hocr and alto, separated by commas or plus signs.
   * Returns false for an empty list or anything else in it.
   */
  static bool parseFormats(const QString& list, ProcessingOptions::Formats* formats);
};

#endif  // SCANTAILOR_CORE_COMMANDLINERUN_H_
