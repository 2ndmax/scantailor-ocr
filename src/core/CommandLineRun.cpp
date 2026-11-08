// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CommandLineRun.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <utility>
#include <vector>

#include "DefaultParams.h"
#include "ExportJob.h"
#include "ImageFileInfo.h"
#include "OcrLanguages.h"
#include "PageSelectionAccessor.h"
#include "PathNames.h"
#include "ProcessingOptions.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectSession.h"
#include "StageSequence.h"

namespace {
QString nativePath(const QString& path) {
  return QDir::toNativeSeparators(path);
}

/** The page as the user finds it: its file, and which page of it. */
QString pageName(const PageId& page) {
  QString name = nativePath(page.imageId().filePath());
  if (page.imageId().isMultiPageFile()) {
    name = CommandLineRun::tr("%1, page %2 of the file").arg(name).arg(page.imageId().page());
  }
  switch (page.subPage()) {
    case PageId::SINGLE_PAGE:
      break;
    case PageId::LEFT_PAGE:
      name = CommandLineRun::tr("%1, left page").arg(name);
      break;
    case PageId::RIGHT_PAGE:
      name = CommandLineRun::tr("%1, right page").arg(name);
      break;
  }
  return name;
}

QString failureMessage(const StageSequence::PageFailure& failure) {
  QString what;
  switch (failure.kind) {
    case StageSequence::PageFailure::LOAD_FAILED:
      what = CommandLineRun::tr("The image could not be loaded.");
      break;
    case StageSequence::PageFailure::PROCESSING_FAILED:
      what = CommandLineRun::tr("The page could not be processed.");
      break;
    case StageSequence::PageFailure::OUTPUT_TOO_LARGE:
      what = CommandLineRun::tr("The output page would be too large with the current settings.");
      break;
    case StageSequence::PageFailure::OUTPUT_NOT_WRITTEN:
      what = CommandLineRun::tr("The output file could not be written.");
      break;
  }
  QString message = pageName(failure.page) + QStringLiteral(": ") + what;
  if (!failure.reasons.isEmpty()) {
    message += QLatin1Char(' ') + failure.reasons.join(QStringLiteral(" "));
  }
  return message;
}

/** The images of the project whose resolution is missing or implausible. */
QStringList filesWithoutDpi(const ProjectPages& pages) {
  QStringList files;
  for (const ImageFileInfo& file : pages.toImageFileInfo()) {
    const std::vector<ImageMetadata>& images = file.imageInfo();
    if (std::any_of(images.begin(), images.end(), [](const ImageMetadata& image) { return !image.isDpiOK(); })) {
      files.push_back(nativePath(file.fileInfo().filePath()));
    }
  }
  return files;
}

/**
 * The checks of the command line that need no project content. Returns the absolute path and
 * name of the exported files through \p exportBase, or false after telling \p err what is wrong.
 */
bool checkExportOptions(const CommandLineRun::Options& options,
                        QString* exportBase,
                        ProcessingOptions::Formats* formats,
                        QTextStream& err) {
  if (!options.exports()) {
    return true;
  }
  if (options.exportDir && options.exportDir->isEmpty()) {
    err << CommandLineRun::tr("The folder after --export-folder is empty.") << Qt::endl;
    return false;
  }
  if (options.formats && !CommandLineRun::parseFormats(*options.formats, formats)) {
    err << CommandLineRun::tr("The list after --export-formats may only hold pdf, txt, hocr and alto: %1")
               .arg(*options.formats)
        << Qt::endl;
    return false;
  }

  // As in the app: next to the project file and named like it.
  const QFileInfo defaultBase(
      project_files::defaultExportBase(QFileInfo(options.projectFile).absoluteFilePath(), QString()));
  const QString name = ExportPaths::nameFromInput(options.exportName ? *options.exportName : defaultBase.fileName());
  const QString folder = options.exportDir ? QFileInfo(*options.exportDir).absoluteFilePath() : defaultBase.path();
  QString reason = path_names::invalidNameReason(name);
  if (reason.isEmpty()) {
    reason = path_names::invalidNewPathReason(folder);
  }
  if (!reason.isEmpty()) {
    err << reason << Qt::endl;
    return false;
  }
  // Without --export-folder it is the folder of the project file. If that is missing, reading the project
  // fails, which is told with the right exit code.
  if (options.exportDir && !QFileInfo(folder).isDir()) {
    err << CommandLineRun::tr("The folder %1 doesn't exist.").arg(nativePath(folder)) << Qt::endl;
    return false;
  }
  *exportBase = QDir(folder).filePath(name);
  return true;
}

/** What the export of \p formats creates, for telling the user. */
QString describeOutputs(const ExportPaths& paths, const ProcessingOptions::Formats& formats) {
  QStringList outputs;
  for (const auto& [chosen, path] : {std::pair(formats.pdf, paths.pdf), std::pair(formats.txt, paths.txt),
                                     std::pair(formats.hocr, paths.hocr), std::pair(formats.alto, paths.altoDir)}) {
    if (chosen) {
      outputs.push_back(nativePath(path));
    }
  }
  return outputs.join(QStringLiteral(", "));
}

/**
 * Whether the export would replace the project file or one of its images, e.g. after a typing error.
 * QFileInfo compares existing files by their canonical path, so different spellings of the same file are
 * found. Only files that exist are compared: QFileInfo takes two missing files as equal, and a missing
 * image would make any export look like it replaced that image.
 * If so, it tells \p err which file it is.
 */
bool exportReplacesProjectFile(const ExportPaths& paths,
                               const ProcessingOptions::Formats& formats,
                               const QString& projectFile,
                               const ProjectPages& pages,
                               QTextStream& err) {
  const std::vector<ImageFileInfo> images = pages.toImageFileInfo();
  const auto replaces = [&](const QString& path, const bool asFolderOfTheFile) {
    const QFileInfo target(path);
    const auto hits = [&](const QFileInfo& file) {
      const QFileInfo other = asFolderOfTheFile ? QFileInfo(file.absolutePath()) : file;
      return target.exists() && other.exists() && (target == other);
    };
    if (hits(QFileInfo(projectFile))) {
      err << CommandLineRun::tr("The export would replace the project file: %1").arg(nativePath(path)) << Qt::endl;
      return true;
    }
    for (const ImageFileInfo& file : images) {
      if (hits(file.fileInfo())) {
        err << CommandLineRun::tr("The export would replace an image of the project: %1").arg(nativePath(path))
            << Qt::endl;
        return true;
      }
    }
    return false;
  };
  return (formats.pdf && replaces(paths.pdf, false)) || (formats.txt && replaces(paths.txt, false))
         || (formats.hocr && replaces(paths.hocr, false))
         // The ALTO folder is replaced as a whole, with everything in it.
         || (formats.alto && (replaces(paths.altoDir, false) || replaces(paths.altoDir, true)));
}

/**
 * Sets up the text recognition as the formats need it. The app asks about languages that aren't
 * installed and offers to download them; here, they end the export.
 * Returns false after telling \p err why it can't be done.
 */
bool setUpOcr(const ProcessingOptions& project, ExportOptions* options, QTextStream& err) {
  if (!options->formats.needsTextRecognition(options->pdf.textLayer)) {
    return true;
  }
  const QStringList missing = OcrLanguages::missing(project.ocr.languages);
  if (!missing.isEmpty()) {
    QStringList names;
    for (const QString& code : missing) {
      names.push_back(OcrLanguages::displayName(code));
    }
    err << CommandLineRun::tr(
               "The languages %1 of the project aren't installed on this computer. Download them in "
               "ScanTailor OCR, in the Export step with \"More languages\", or turn them off there.")
               .arg(names.join(QStringLiteral(", ")))
        << Qt::endl;
    return false;
  }
  QString error;
  switch (options->setUpOcr(project.ocr.languages, project.ocr.pageLayout, &error)) {
    case ExportOptions::OcrSetup::DONE:
      return true;
    case ExportOptions::OcrSetup::NO_LANGUAGE:
      err << CommandLineRun::tr(
          "The export needs text recognition, but the project has no language ticked. Tick one in "
          "ScanTailor OCR, in the Export step.")
          << Qt::endl;
      return false;
    case ExportOptions::OcrSetup::NOT_GATHERED:
      err << CommandLineRun::tr(
          "The files of the selected languages are in different folders, and they could not be "
          "copied into one folder, which text recognition needs.")
          << ' ' << error << Qt::endl;
      return false;
  }
  return false;
}

/** Creates \p formats from the output files, with the project's settings. */
CommandLineRun::ExitCode exportFiles(const ExportPaths& paths,
                                     const ProcessingOptions::Formats& formats,
                                     const ProjectSession& session,
                                     QTextStream& out,
                                     QTextStream& err,
                                     const std::atomic<bool>& cancel) {
  // Ctrl+C during the last page of processing: that page was still output, but no export is wanted.
  if (cancel) {
    err << CommandLineRun::tr("Canceled. Nothing was exported.") << Qt::endl;
    return CommandLineRun::CANCELLED;
  }

  std::vector<ExportPage> pages = session.stages->pdfExportPages(*session.pages, session.outFileNameGen);
  // Not expected after all pages were output, but the export would lack them or show an older state.
  bool complete = true;
  for (const ExportPage& page : pages) {
    if ((page.kind() == ExportPage::MISSING) || page.isOutdated()) {
      err << CommandLineRun::tr("%1: The output file is missing or outdated.").arg(pageName(page.pageId())) << Qt::endl;
      complete = false;
    }
  }
  if (!complete) {
    return CommandLineRun::EXPORT_FAILED;
  }

  const ProcessingOptions& project = *session.stages->processingOptions();
  ExportOptions options = ExportOptions::fromProject(project);
  options.formats = formats;
  options.threadCount = ExportJob::defaultThreadCount();
  if (!setUpOcr(project, &options, err)) {
    return CommandLineRun::EXPORT_FAILED;
  }

  ExportJob job(std::move(pages), paths, options);
  // REVIEW-NOTE (2026-11): Ctrl+C while a page is being recognized -> the tool only cancels the job after that
  // page is written, so text recognition of the current page runs to its end. Not fixed because ExportJob only
  // takes a cancel request through cancel(), and the delay is at most one page: a request after the last page
  // still stops the job before any file is replaced.
  // run() emits it in this thread, so the lambda is called directly.
  QObject::connect(&job, &ExportJob::progress, [&](const int pagesDone, const int pagesTotal) {
    out << CommandLineRun::tr("Export: %1 of %2").arg(pagesDone).arg(pagesTotal) << Qt::endl;
    if (cancel) {
      job.cancel();
    }
  });
  job.run();

  if (job.succeeded()) {
    out << CommandLineRun::tr("Created: %1").arg(describeOutputs(paths, formats)) << Qt::endl;
    return CommandLineRun::SUCCESS;
  }
  if (job.wasCancelled()) {
    err << CommandLineRun::tr("Canceled. Nothing was exported.") << Qt::endl;
    return CommandLineRun::CANCELLED;
  }
  err << CommandLineRun::tr("The export failed.") << Qt::endl;
  for (const QString& error : job.errors()) {
    err << "  " << error << Qt::endl;
  }
  return CommandLineRun::EXPORT_FAILED;
}
}  // namespace

bool CommandLineRun::parseFormats(const QString& list, ProcessingOptions::Formats* formats) {
  ProcessingOptions::Formats parsed;
  parsed.pdf = false;
  static const QRegularExpression separator(QStringLiteral("[,+]"));
  for (const QString& name : list.split(separator)) {
    const QString format = name.trimmed().toLower();
    if (format == QLatin1String("pdf")) {
      parsed.pdf = true;
    } else if (format == QLatin1String("txt")) {
      parsed.txt = true;
    } else if (format == QLatin1String("hocr")) {
      parsed.hocr = true;
    } else if (format == QLatin1String("alto")) {
      parsed.alto = true;
    } else {
      return false;
    }
  }
  *formats = parsed;
  return true;
}

CommandLineRun::ExitCode CommandLineRun::run(const Options& options,
                                             QTextStream& out,
                                             QTextStream& err,
                                             const std::atomic<bool>& cancel) {
  QString exportBase;
  ProcessingOptions::Formats namedFormats;
  if (!checkExportOptions(options, &exportBase, &namedFormats, err)) {
    return USAGE_ERROR;
  }
  const bool exporting = !exportBase.isEmpty();

  const ProjectReader::FileResult read = ProjectReader::readFile(options.projectFile);
  if (read.error != ProjectReader::FileError::NONE) {
    err << nativePath(options.projectFile) << ": " << ProjectReader::errorMessage(read.error) << Qt::endl;
    return PROJECT_UNUSABLE;
  }
  const ProjectReader& reader = *read.reader;

  // The app asks what to do with these; here they are left to the app.
  if (reader.isFromOlderVersion()) {
    err << tr(
        "The project was saved by ScanTailor Advanced or an older version of ScanTailor OCR. Open it once "
        "in ScanTailor OCR and save it there, then it can be processed here.")
        << Qt::endl;
    return PROJECT_UNUSABLE;
  }
  // Saved here, the project would lose these images from the file without a word.
  if (reader.numDamagedImages() > 0) {
    err << tr(
        "The project file is damaged: %n image(s) can't be read. Open the project in ScanTailor OCR, which "
        "offers a backup copy, and save it there, then it can be processed here.",
        nullptr, reader.numDamagedImages())
        << Qt::endl;
    return PROJECT_UNUSABLE;
  }
  if (reader.pages()->numImages() == 0) {
    err << tr("The project has no images.") << Qt::endl;
    return PROJECT_UNUSABLE;
  }
  const QStringList noDpi = filesWithoutDpi(*reader.pages());
  if (!noDpi.isEmpty()) {
    err << tr(
        "The resolution (DPI) of these images is missing or implausible. Set it once in ScanTailor OCR, "
        "then the project can be processed here:")
        << Qt::endl;
    for (const QString& file : noDpi) {
      err << "  " << file << Qt::endl;
    }
    return PROJECT_UNUSABLE;
  }
  ProcessingOptions::Formats formats = namedFormats;
  ExportPaths exportPaths;
  if (exporting) {
    if (!options.formats) {
      // REVIEW-NOTE (2026-11): a project saved by 1.0.10 to 1.0.12 has no <formats> -> PDF only here, the default
      // profile's formats in the app. Not fixed because no such projects are in use.
      formats = reader.readProcessingOptions(ProcessingOptions()).formats;
      if (!formats.any()) {
        err << tr(
            "The project has no format ticked. Tick one in ScanTailor OCR, in the Export step, or name them "
            "with --export-formats.")
            << Qt::endl;
        // REVIEW-NOTE (2026-11): a project with no format ticked, exported without --export-formats -> exit code 1
        // (wrong command line) although the command line itself is correct. Not fixed because the message names
        // --export-formats as the remedy and scripts usually only test for "not 0".
        return USAGE_ERROR;
      }
    }
    exportPaths = ExportPaths::fromBase(exportBase);
    if (exportReplacesProjectFile(exportPaths, formats, options.projectFile, *reader.pages(), err)) {
      return USAGE_ERROR;
    }
    // Before the processing, which may take long: a wrong name is noticed right away.
    out << tr("The export will create: %1").arg(describeOutputs(exportPaths, formats)) << Qt::endl;
  }

  // The fallback only fills in what projects of older versions lack, and those were refused above.
  const ProjectSession session = ProjectSession::load(reader, DefaultParams(), PageSelectionAccessor(nullptr));
  const StageSequence& stages = *session.stages;
  // Loading has already recreated a deleted output folder if the project is where it was. Otherwise
  // the output would go to a folder created anew in the old place, and the images may be missing.
  if (project_files::needsRelinking(*session.pages, session.outFileNameGen.outDir())) {
    err << tr(
        "The output folder or some images of the project are missing, for example because the project was "
        "moved. Open it in ScanTailor OCR, relink it there and save it, then it can be processed here.")
        << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  const StageSequence::ProgressCallback progress = [&](const int lastFilterIdx, const int done, const int total) {
    out << tr("%1: %2 of %3").arg(stages.filterAt(lastFilterIdx)->getName()).arg(done).arg(total) << Qt::endl;
    return !cancel;
  };
  // Without thumbnails: they are pixmaps, which need a GUI.
  const StageSequence::ProcessAllResult processed = stages.processAll(nullptr, session.outFileNameGen, progress);
  for (const StageSequence::PageFailure& failure : processed.failures) {
    err << failureMessage(failure) << Qt::endl;
  }

  // Also after a failure or cancelling: the steps stored their results for the pages done.
  if (!session.write(options.projectFile)) {
    err << tr("The project file could not be saved: %1").arg(nativePath(options.projectFile)) << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  if (processed.cancelled) {
    err << tr("Canceled. The pages output so far keep their output files.") << Qt::endl;
    return CANCELLED;
  }
  if (!processed.failures.empty() || !processed.output) {
    if (processed.failures.empty()) {
      err << tr("No page was output.") << Qt::endl;
    } else {
      err << tr("Pages that failed: %1").arg(processed.failures.size()) << Qt::endl;
    }
    if (exporting) {
      err << tr("Nothing was exported, as not all pages were output.") << Qt::endl;
    }
    return PAGES_FAILED;
  }
  out << tr("Done. The output is in %1").arg(nativePath(session.outFileNameGen.outDir())) << Qt::endl;

  if (!exporting) {
    return SUCCESS;
  }
  return exportFiles(exportPaths, formats, session, out, err, cancel);
}
