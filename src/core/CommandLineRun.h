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
    /** The folder --profile looks up a name in; empty for the profile folder of the program. For tests. */
    QString profileFolder;
    /** --dpi: the DPI of all images of the new project; unset without it. */
    std::optional<Dpi> dpi;
    /** --dpi-fix: the DPI of the images that need fixing; unset without it. */
    std::optional<Dpi> dpiFix;
    /** --color-mode-detect: after the Margins step, as the app's Detect Color Mode does. */
    bool colorModeDetect = false;
    /** --orientation-detect: "pages" or "book"; empty without it. */
    QString orientationDetect;
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

    /**
     * --report-file: the JSON of the run, written once a project could be opened or created, also
     * when pages failed, the export failed or the run was cancelled; unset without it. The keys
     * are English and are never translated. Version 1 of the schema:
     *
     * schemaVersion: integer, always 1.
     * run: object with
     *   mode: string, "project" or "folder".
     *   projectFile: string, the native path of the project.
     *   profile: string, the name or the file of the profile actually used.
     *   lastStep: string, one of "fix-orientation", "split-pages", "deskew", "select-content",
     *       "margins", "output" or "export".
     *   exitCode: integer, one of the ExitCode values above; reports exist for 0, 3, 4 and 5.
     *   cancelled: bool, true when a cancel was requested.
     * pages: array, one object per page in the order of the page view:
     *   file: string, the native path of the image file.
     *   page: integer, the 1-based page of a multi-page file; absent for a single-page file.
     *   subPage: string, "left" or "right"; absent unless the page is split.
     *   dpi: object with the integers horizontal and vertical.
     *   dpiSource: string, "command-line" or "file".
     *   colorMode: string, "black-and-white", "color-grayscale" or "mixed".
     *   colorModeDetected: bool, true when --color-mode-detect supplied colorMode.
     *   rotation: integer, the orientation of the page's image in degrees clockwise, as stored in the project.
     *   rotationDetection: string, only with --orientation-detect: "detected", "book-rule", "undecided" or "failed".
     *   outputFile: string, the native path of the output file.
     *   status: string, "ok" or "failed".
     *   errorKind: string, only for status "failed": "LOAD_FAILED", "PROCESSING_FAILED",
     *       "OUTPUT_TOO_LARGE" or "OUTPUT_NOT_WRITTEN".
     *   reasons: array of strings, only for status "failed": why that page failed.
     * exports: array of strings, the native paths created by a successful export; empty otherwise.
     * passedOver: array of strings, the native paths of the other files of a folder-mode input folder;
     *       empty in project mode.
     */
    std::optional<QString> reportFile;

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
  static ExitCode run(const Options& options,
                      QTextStream& out,
                      QTextStream& err,
                      const std::atomic<bool>& cancel,
                      QTextStream* progressJson = nullptr);

  /**
   * Prints the profiles --profile takes, as JSON on \p out, indented like the report. Exit codes:
   * 0 on success. The schema (version 1):
   *
   * schemaVersion: integer, always 1.
   * profile: string, the profile a run without --profile takes; absent when the program's profile
   *     is unreadable.
   * profiles: array of objects, built-in profiles first (Default, Minimal), then the readable file
   *     profiles sorted by name, with
   *   name: string, what --profile takes.
   *   source: string, "built-in" or "file".
   *
   * File profiles named Default, Minimal or Source are not listed, because --profile with that name
   * takes the built-in profile. Unreadable file profiles are not listed either.
   *
   * \p profileDir: empty means the program's profile folder, otherwise this folder (for tests).
   */
  static ExitCode profileList(QTextStream& out, QTextStream& err, const QString& profileDir = QString());

  /**
   * Prints the pages of the project file, as JSON on \p out, indented like the report. Exit codes:
   * 0 on success, 2 when the project can't be read or must be relinked. \p out stays empty then.
   * The schema (version 1):
   *
   * schemaVersion: integer, always 1.
   * mode: string, "project".
   * projectFile: string, the native path of the project.
   * outputFolder: string, the native path of the output folder (even when it is missing).
   * pages: array of objects, in the order of the page view, with
   *   file: string, the native path of the image file.
   *   page: integer, the 1-based page of a multi-page file; absent for a single-page file.
   *   subPage: string, "left" or "right"; absent unless the page is split.
   *   dpi: object with the integers horizontal and vertical.
   *   dpiSource: string, always "file".
   *   colorMode: string, "black-and-white", "color-grayscale" or "mixed".
   *   outputFile: string, the native path of the output file.
   *
   * Nothing is created or saved: no output folder, cache or thumbnails.
   */
  static ExitCode projectInfo(const QString& projectFile, QTextStream& out, QTextStream& err);

  /**
   * Reads a list of the formats pdf, txt, hocr and alto, separated by commas or plus signs.
   * Returns false for an empty list or anything else in it.
   */
  static bool parseFormats(const QString& list, ProcessingOptions::Formats* formats);
};

#endif  // SCANTAILOR_CORE_COMMANDLINERUN_H_
