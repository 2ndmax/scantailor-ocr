// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CommandLineRun.h"

#include <QDir>
#include <QTextStream>
#include <algorithm>

#include "DefaultParams.h"
#include "ImageFileInfo.h"
#include "PageSelectionAccessor.h"
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

  // The fallback only fills in what projects of older versions lack, and those were refused above.
  const ProjectSession session = ProjectSession::load(reader, DefaultParams(), PageSelectionAccessor(nullptr));
  const StageSequence& stages = *session.stages;

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
  if (!processed.failures.empty()) {
    err << tr("Pages that failed: %1").arg(processed.failures.size()) << Qt::endl;
    return PAGES_FAILED;
  }
  if (!processed.output) {
    err << tr("No page was output.") << Qt::endl;
    return PAGES_FAILED;
  }
  out << tr("Done. The output is in %1").arg(nativePath(session.outFileNameGen.outDir())) << Qt::endl;
  return SUCCESS;
}
