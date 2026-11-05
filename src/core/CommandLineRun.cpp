// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CommandLineRun.h"

#include <QDir>
#include <QFileInfo>
#include <QTextStream>
#include <algorithm>

#include "DefaultParams.h"
#include "ExportJob.h"
#include "ImageFileInfo.h"
#include "OcrLanguages.h"
#include "PageSelectionAccessor.h"
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
 * Whether the PDF would replace the project file or one of its images, e.g. after a typing error.
 * A file that doesn't exist yet can't be one of them; QFileInfo compares existing files by their
 * canonical path, so different spellings of the same file are found.
 * If so, it tells \p err which file it is.
 */
bool pdfReplacesProjectFile(const QString& pdfFile,
                            const QString& projectFile,
                            const ProjectPages& pages,
                            QTextStream& err) {
  const QFileInfo pdf(pdfFile);
  if (pdf == QFileInfo(projectFile)) {
    err << CommandLineRun::tr("The PDF would replace the project file: %1").arg(nativePath(pdfFile)) << Qt::endl;
    return true;
  }
  for (const ImageFileInfo& file : pages.toImageFileInfo()) {
    if (pdf == file.fileInfo()) {
      err << CommandLineRun::tr("The PDF would replace an image of the project: %1").arg(nativePath(pdfFile))
          << Qt::endl;
      return true;
    }
  }
  return false;
}

/**
 * Sets up the text recognition of the PDF as the project wants it. The app asks about languages
 * that aren't installed and offers to download them; here, they end the export.
 * Returns false after telling \p err why it can't be done.
 */
bool setUpOcr(const ProcessingOptions::Pdf& projectPdf, ExportOptions* options, QTextStream& err) {
#ifdef ENABLE_OCR
  if (!projectPdf.ocr) {
    return true;
  }
  const QStringList missing = OcrLanguages::missing(projectPdf.ocrLanguages);
  if (!missing.isEmpty()) {
    QStringList names;
    for (const QString& code : missing) {
      names.push_back(OcrLanguages::displayName(code));
    }
    err << CommandLineRun::tr(
               "The languages %1 of the project aren't installed on this computer. Download them in "
               "ScanTailor OCR, in the PDF export with \"More languages\", or turn them off there.")
               .arg(names.join(QStringLiteral(", ")))
        << Qt::endl;
    return false;
  }
  QString error;
  switch (options->setUpOcr(projectPdf.ocrLanguages, projectPdf.ocrPageLayout, &error)) {
    case ExportOptions::OcrSetup::DONE:
      return true;
    case ExportOptions::OcrSetup::NO_LANGUAGE:
      err << CommandLineRun::tr(
          "The project has text recognition turned on, but no language ticked. Tick one in "
          "ScanTailor OCR, in the PDF export, or turn the text recognition off there.")
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
#else
  // As in the app, which offers no text recognition then.
  Q_UNUSED(projectPdf);
  Q_UNUSED(options);
  Q_UNUSED(err);
  return true;
#endif
}

/** Creates the PDF \p pdfFile from the output files, with the project's PDF settings. */
CommandLineRun::ExitCode exportPdf(const QString& pdfFile,
                                   const ProjectSession& session,
                                   QTextStream& out,
                                   QTextStream& err,
                                   const std::atomic<bool>& cancel) {
  // Ctrl+C during the last page of processing: that page was still output, but no PDF is wanted.
  if (cancel) {
    err << CommandLineRun::tr("Canceled. No PDF was written.") << Qt::endl;
    return CommandLineRun::CANCELLED;
  }

  std::vector<ExportPage> pages = session.stages->pdfExportPages(*session.pages, session.outFileNameGen);
  // Not expected after all pages were output, but the PDF would lack them or show an older state.
  bool complete = true;
  for (const ExportPage& page : pages) {
    if ((page.kind() == ExportPage::MISSING) || page.isOutdated()) {
      err << CommandLineRun::tr("%1: The output file is missing or outdated.").arg(pageName(page.pageId())) << Qt::endl;
      complete = false;
    }
  }
  if (!complete) {
    return CommandLineRun::PDF_FAILED;
  }

  const ProcessingOptions::Pdf& projectPdf = session.stages->processingOptions()->pdf;
  ExportOptions options = ExportOptions::fromProject(*session.stages->processingOptions());
  options.threadCount = ExportJob::defaultThreadCount();
  if (!setUpOcr(projectPdf, &options, err)) {
    return CommandLineRun::PDF_FAILED;
  }

  ExportJob job(std::move(pages), pdfFile, options);
  // REVIEW-NOTE (2026-11): Ctrl+C while a PDF page is being recognized -> the tool only cancels the job after that
  // page is written, so text recognition of the current page runs to its end. Not fixed because ExportJob only
  // takes a cancel request through cancel(), and the delay is at most one page.
  // run() emits it in this thread, so the lambda is called directly.
  QObject::connect(&job, &ExportJob::progress, [&](const int pagesDone, const int pagesTotal) {
    out << CommandLineRun::tr("PDF: %1 of %2").arg(pagesDone).arg(pagesTotal) << Qt::endl;
    if (cancel) {
      job.cancel();
    }
  });
  job.run();

  if (job.succeeded()) {
    out << CommandLineRun::tr("The PDF was created: %1").arg(nativePath(pdfFile)) << Qt::endl;
    return CommandLineRun::SUCCESS;
  }
  if (job.wasCancelled()) {
    err << CommandLineRun::tr("Canceled. No PDF was written.") << Qt::endl;
    return CommandLineRun::CANCELLED;
  }
  err << CommandLineRun::tr("The PDF could not be created.") << Qt::endl;
  for (const QString& error : job.errors()) {
    err << "  " << error << Qt::endl;
  }
  return CommandLineRun::PDF_FAILED;
}
}  // namespace

CommandLineRun::ExitCode CommandLineRun::run(const Options& options,
                                             QTextStream& out,
                                             QTextStream& err,
                                             const std::atomic<bool>& cancel) {
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
  if (!options.pdfFile.isEmpty()
      && pdfReplacesProjectFile(options.pdfFile, options.projectFile, *reader.pages(), err)) {
    return USAGE_ERROR;
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
    if (!options.pdfFile.isEmpty()) {
      err << tr("No PDF was created, as not all pages were output.") << Qt::endl;
    }
    return PAGES_FAILED;
  }
  out << tr("Done. The output is in %1").arg(nativePath(session.outFileNameGen.outDir())) << Qt::endl;

  if (options.pdfFile.isEmpty()) {
    return SUCCESS;
  }
  return exportPdf(QFileInfo(options.pdfFile).absoluteFilePath(), session, out, err, cancel);
}
