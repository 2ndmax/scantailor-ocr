// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COMMANDLINERUN_H_
#define SCANTAILOR_CORE_COMMANDLINERUN_H_

#include <QCoreApplication>
#include <QString>
#include <atomic>
#include <optional>

#include "ProcessingOptions.h"

class QTextStream;

/**
 * What the command line tool does: it opens a project, processes all its pages through all
 * six steps and saves the project. It runs in the calling thread and needs only a
 * QCoreApplication, so it can be tested without the tool itself.
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
    QString projectFile;
    /**
     * Export with the settings of the project (--export). Each of the other export options
     * (--export-folder, --export-name, --export-formats) turns it on as well.
     */
    bool exportRequested = false;
    /**
     * The existing folder for the exported files. Not set for the folder of the project file; a set
     * but empty value is an error, e.g. from an empty variable in a script.
     */
    std::optional<QString> exportDir;
    /** The name of the exported files, see ExportPaths::fromBase(). Not set for the name of the project file. */
    std::optional<QString> exportName;
    /** The formats as for parseFormats(). Not set for the formats of the project. */
    std::optional<QString> formats;

    /** Whether the pages are exported after processing. */
    bool exports() const { return exportRequested || exportDir || exportName || formats; }
  };

  /**
   * Runs the tool. The progress goes to \p out, errors go to \p err. Setting \p cancel stops
   * processing before the next page; the pages done so far keep their results, and the project
   * is saved with them. The export only runs if all pages were output.
   */
  static ExitCode run(const Options& options, QTextStream& out, QTextStream& err, const std::atomic<bool>& cancel);

  /**
   * Reads a list of the formats pdf, txt, hocr and alto, separated by commas or plus signs.
   * Returns false for an empty list or anything else in it.
   */
  static bool parseFormats(const QString& list, ProcessingOptions::Formats* formats);
};

#endif  // SCANTAILOR_CORE_COMMANDLINERUN_H_
