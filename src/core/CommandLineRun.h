// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COMMANDLINERUN_H_
#define SCANTAILOR_CORE_COMMANDLINERUN_H_

#include <QCoreApplication>
#include <QString>
#include <atomic>

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
    /** The PDF couldn't be created. */
    PDF_FAILED = 4,
    /** Processing was cancelled. */
    CANCELLED = 5
  };

  struct Options {
    QString projectFile;
  };

  /**
   * Runs the tool. The progress goes to \p out, errors go to \p err. Setting \p cancel stops
   * processing before the next page; the pages done so far keep their results, and the project
   * is saved with them.
   */
  static ExitCode run(const Options& options, QTextStream& out, QTextStream& err, const std::atomic<bool>& cancel);
};

#endif  // SCANTAILOR_CORE_COMMANDLINERUN_H_
