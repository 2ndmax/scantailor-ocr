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

#include <ApplicationSettings.h>
#include <CoreUtils.h>
#include <DefaultParamsProfileManager.h>
#include <DefaultParamsProvider.h>
#include <Dpi.h>
#include <SmartFilenameOrdering.h>

#include <QFile>

#include "DefaultParams.h"
#include "ExportJob.h"
#include "ImageFileInfo.h"
#include "OcrLanguages.h"
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

/** Whether the two folders are the same, by their absolute cleaned paths. */
bool isSameFolder(const QString& lhs, const QString& rhs) {
  return QDir::cleanPath(QFileInfo(lhs).absoluteFilePath()) == QDir::cleanPath(QFileInfo(rhs).absoluteFilePath());
}

/** Whether the DPI \p dpi is red for an image of the pixel size \p size, as ImageMetadata says. */
bool dpiIsRedFor(const QSize& size, const Dpi& dpi) {
  return !ImageMetadata(size, dpi).isDpiOK();
}

/**
 * The importable images of the folder of a new project, sorted as the app sorts them, loaded
 * with their metadata. Refuses, after telling \p err, a file that can't be read or is only
 * partly readable, and when there is none. \p passedOver is filled with the other files of the
 * folder, which are only counted.
 */
bool loadFolderFiles(const QString& inputDir,
                     QTextStream& err,
                     std::vector<ImageFileInfo>* files,
                     QStringList* passedOver) {
  QFileInfoList all(QDir(inputDir).entryInfoList(QDir::Files));
  std::sort(all.begin(), all.end(), SmartFilenameOrdering());
  for (const QFileInfo& file : all) {
    // The output of the default output folder lies in the input folder; a hidden file is no scan.
    if (!project_files::isImportableImage(file) || file.isHidden()) {
      passedOver->append(file.filePath());
      continue;
    }
    QStringList loadErrors;
    QStringList partialReads;
    std::optional<ImageFileInfo> loaded = ImageFileInfo::load(file, &loadErrors, &partialReads);
    if (!loaded.has_value()) {
      for (const QString& reason : loadErrors) {
        err << file.filePath() << ": " << reason << Qt::endl;
      }
      return false;
    }
    if (!loaded->partialReads().isEmpty()) {
      err << file.filePath() << ": " << CommandLineRun::tr("The file is only partly readable.") << Qt::endl;
      for (const QString& reason : loaded->partialReads()) {
        err << "  " << reason << Qt::endl;
      }
      return false;
    }
    files->push_back(std::move(*loaded));
  }
  if (files->empty()) {
    err << CommandLineRun::tr("The input folder has no images: %1").arg(nativePath(inputDir)) << Qt::endl;
    return false;
  }
  return true;
}

/**
 * The new project of the folder: the files of the input folder, the profile and the output
 * folder, as the Project Files dialog of the app has them, without its questions. Returns false
 * after telling \p err why it can't be done; \p passedOver is filled with the other files of
 * the input folder, which are only counted.
 */
std::optional<ProjectSession> createFolderProject(const CommandLineRun::Options& options,
                                                  const QString& projectFile,
                                                  QTextStream& out,
                                                  QTextStream& err,
                                                  QStringList* passedOver) {
  const QString inputDir = QFileInfo(options.inputFolder).absoluteFilePath();
  if (!QDir(inputDir).exists()) {
    err << CommandLineRun::tr("The input folder doesn't exist: %1").arg(nativePath(inputDir)) << Qt::endl;
    return std::nullopt;
  }
  // The output folder: --output-folder, created, or the default <input>/out.
  QString outDir = project_files::defaultOutputDir(inputDir);
  if (options.outputFolder) {
    const QString folder = QFileInfo(*options.outputFolder).absoluteFilePath();
    const QString reason = path_names::invalidNewPathReason(folder);
    if (!reason.isEmpty()) {
      err << reason << Qt::endl;
      return std::nullopt;
    }
    if (isSameFolder(folder, inputDir)) {
      err << CommandLineRun::tr("The input and the output folders can't be the same.") << Qt::endl;
      return std::nullopt;
    }
    if (!QDir(folder).mkpath(folder)) {
      err << CommandLineRun::tr("Unable to create the output folder: %1").arg(nativePath(folder)) << Qt::endl;
      return std::nullopt;
    }
    outDir = folder;
  }
  // The project file: --project-file or the default of the folder; the default the app creates
  // on the fly, here it must not exist yet.
  const QString reason = path_names::invalidNewPathReason(projectFile);
  if (!reason.isEmpty()) {
    err << reason << Qt::endl;
    return std::nullopt;
  }
  if (QFileInfo(projectFile).exists()) {
    err << CommandLineRun::tr("The project file exists: %1. Call the tool with it to process the project.")
               .arg(nativePath(projectFile))
        << Qt::endl;
    return std::nullopt;
  }
  if (!QFileInfo(projectFile).absoluteDir().exists()) {
    err << CommandLineRun::tr("The folder of the project file doesn't exist: %1")
               .arg(nativePath(QFileInfo(projectFile).absoluteDir().absolutePath()))
        << Qt::endl;
    return std::nullopt;
  }

  // The profile: --profile or the one of the program.
  DefaultParams startValues;
  QString profileName;
  if (!options.profile.isEmpty()) {
    DefaultParamsProfileManager manager;
    std::unique_ptr<DefaultParams> profile;
    if (options.profile.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive)) {
      QFile file(options.profile);
      if (file.open(QIODevice::ReadOnly)) {
        profile = DefaultParamsProfileManager::readProfile(file);
      }
      if (!profile) {
        err << CommandLineRun::tr("The profile file doesn't exist or can't be read: %1")
                   .arg(nativePath(options.profile))
            << Qt::endl;
        return std::nullopt;
      }
      profileName = options.profile;
    } else {
      // The built-in profiles, and their old name: "Source" is the old name of "Minimal".
      const QString name = (options.profile == QLatin1String("Source")) ? QStringLiteral("Minimal") : options.profile;
      if (name == QLatin1String("Default")) {
        profile = manager.createDefaultProfile();
      } else if (name == QLatin1String("Minimal")) {
        profile = manager.createMinimalProfile();
      } else {
        profile = manager.readProfile(name);
      }
      if (!profile) {
        err << CommandLineRun::tr("The profile %1 isn't available.").arg(options.profile) << Qt::endl;
        return std::nullopt;
      }
      profileName = name;
    }
    startValues = std::move(*profile);
  } else {
    // The profile of the program: refuse it when it is unreadable, as startProfile() reports it
    // (the name in use differs from the one stored).
    if (ApplicationSettings::getInstance().getCurrentProfile()
        != DefaultParamsProvider::getInstance().getProfileName()) {
      err << CommandLineRun::tr(
          "The profile of the program isn't readable; pass --profile with the name or the "
          "file of one.")
          << Qt::endl;
      return std::nullopt;
    }
    startValues = DefaultParamsProvider::getInstance().getParams();
    profileName = DefaultParamsProvider::getInstance().getProfileName();
  }
  out << CommandLineRun::tr("Profile: %1").arg(profileName) << Qt::endl;

  std::vector<ImageFileInfo> files;
  if (!loadFolderFiles(inputDir, err, &files, passedOver)) {
    return std::nullopt;
  }

  // The red and the yellow of the files: red is no DPI at all or too small to process, yellow a
  // doubtful one.
  QStringList yellowFiles;
  bool redFound = false;
  for (const ImageFileInfo& file : files) {
    bool yellow = false;
    for (const ImageMetadata& metadata : file.imageInfo()) {
      if (!metadata.isDpiOK()) {
        redFound = true;
      } else if (metadata.worseDpiStatus() != ImageMetadata::DPI_OK) {
        yellow = true;
      }
    }
    if (yellow) {
      yellowFiles.append(nativePath(file.fileInfo().absoluteFilePath()));
    }
  }

  // --dpi: the value must not be red for any image; it is the DPI of all of them.
  if (options.dpi) {
    for (const ImageFileInfo& file : files) {
      for (const ImageMetadata& metadata : file.imageInfo()) {
        if (dpiIsRedFor(metadata.size(), *options.dpi)) {
          err << CommandLineRun::tr(
                     "The DPI value %1 is red for the image %2: it is missing or too small to "
                     "process.")
                     .arg(core::CoreUtils::dpiText(options.dpi->horizontal()))
                     .arg(nativePath(file.fileInfo().absoluteFilePath()))
              << Qt::endl;
          return std::nullopt;
        }
      }
    }
  }
  // --dpi-fix: the value for the images that need fixing; one that is red for them is refused.
  if (options.dpiFix) {
    for (const ImageFileInfo& file : files) {
      for (const ImageMetadata& metadata : file.imageInfo()) {
        if (metadata.needsFixing() && dpiIsRedFor(metadata.size(), *options.dpiFix)) {
          err << CommandLineRun::tr(
                     "The DPI value %1 is red for the image %2: it is missing or too small to "
                     "process.")
                     .arg(core::CoreUtils::dpiText(options.dpiFix->horizontal()))
                     .arg(nativePath(file.fileInfo().absoluteFilePath()))
              << Qt::endl;
          return std::nullopt;
        }
      }
    }
  }
  if (!options.dpi && !options.dpiFix && redFound) {
    err << CommandLineRun::tr(
        "The resolution (DPI) of these images is missing or too small to process; set it "
        "once in ScanTailor OCR, or pass --dpi:")
        << Qt::endl;
    for (const ImageFileInfo& file : files) {
      for (const ImageMetadata& metadata : file.imageInfo()) {
        if (!metadata.isDpiOK()) {
          err << "  " << nativePath(file.fileInfo().absoluteFilePath()) << Qt::endl;
          break;
        }
      }
    }
    return std::nullopt;
  }
  // The DPI of the files: --dpi for all of them, --dpi-fix for the ones that need fixing.
  QStringList fixedFiles;
  for (ImageFileInfo& file : files) {
    for (ImageMetadata& metadata : file.imageInfo()) {
      if (options.dpi) {
        metadata.setDpi(*options.dpi);
      } else if (options.dpiFix && metadata.needsFixing()) {
        metadata.setDpi(*options.dpiFix);
        if (!fixedFiles.contains(file.fileInfo().absoluteFilePath())) {
          fixedFiles.append(file.fileInfo().absoluteFilePath());
        }
      }
    }
  }
  if (!options.dpi && !options.dpiFix && !yellowFiles.isEmpty()) {
    err << CommandLineRun::tr("Warning: the resolution of these files is doubtful:") << Qt::endl;
    for (const QString& file : yellowFiles) {
      err << "  " << file << Qt::endl;
    }
  }
  if (options.dpi) {
    out << CommandLineRun::tr("DPI: %1").arg(core::CoreUtils::dpiText(options.dpi->horizontal())) << Qt::endl;
  }
  if (options.dpiFix && !fixedFiles.isEmpty()) {
    out << CommandLineRun::tr("Fixed the DPI of %n file(s) to %1:", nullptr, fixedFiles.size())
               .arg(core::CoreUtils::dpiText(options.dpiFix->horizontal()))
        << Qt::endl;
    for (const QString& file : fixedFiles) {
      out << "  " << nativePath(file) << Qt::endl;
    }
  }

  // The project: its pages take the files, whose DPI is set; its start values and page order
  // the profile.
  const std::shared_ptr<ProjectPages> pages
      = ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder());
  return ProjectSession::createNew(pages, outDir, inputDir, startValues);
}

/**
 * The session of a project file: refuses, after telling \p err, what it can't be processed with
 * (an older or damaged project, one without images or without a usable DPI).
 */
std::optional<ProjectSession> loadExistingProject(const ProjectReader& reader, QTextStream& err) {
  // The app asks what to do with these; here they are left to the app.
  if (reader.isFromOlderVersion()) {
    err << CommandLineRun::tr(
        "The project was saved by ScanTailor Advanced or an older version of ScanTailor OCR. Open it once "
        "in ScanTailor OCR and save it there, then it can be processed here.")
        << Qt::endl;
    return std::nullopt;
  }
  // Saved here, the project would lose these images from the file without a word.
  if (reader.numDamagedImages() > 0) {
    err << CommandLineRun::tr(
        "The project file is damaged: %n image(s) can't be read. Open the project in ScanTailor OCR, which "
        "offers a backup copy, and save it there, then it can be processed here.",
        nullptr, reader.numDamagedImages())
        << Qt::endl;
    return std::nullopt;
  }
  if (reader.pages()->numImages() == 0) {
    err << CommandLineRun::tr("The project has no images.") << Qt::endl;
    return std::nullopt;
  }
  const QStringList noDpi = filesWithoutDpi(*reader.pages());
  if (!noDpi.isEmpty()) {
    err << CommandLineRun::tr(
        "The resolution (DPI) of these images is missing or implausible. Set it once in ScanTailor OCR, "
        "then the project can be processed here:")
        << Qt::endl;
    for (const QString& file : noDpi) {
      err << "  " << file << Qt::endl;
    }
    return std::nullopt;
  }
  // The fallback fills in what a project of 1.0.9 to 1.0.12 lacks.
  return ProjectSession::load(reader, DefaultParams());
}

/**
 * The checks of the command line that need no project content. Returns the absolute path and
 * name of the exported files through \p exportBase, or false after telling \p err what is wrong.
 */
bool checkExportOptions(const CommandLineRun::Options& options,
                        const QString& projectFile,
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
  const QFileInfo defaultBase(project_files::defaultExportBase(QFileInfo(projectFile).absoluteFilePath(), QString()));
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
  // page is written, so text recognition of the current page runs to its end. Ctrl+C after the progress signal of
  // the last page -> the job is not told any more, the files are replaced and the exit code is 0. Not fixed because
  // ExportJob only takes a cancel request through cancel(), the first delay is at most one page and the second gap
  // lasts milliseconds.
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

QString CommandLineRun::Options::stepName(const LastStep step) {
  switch (step) {
    case FIX_ORIENTATION:
      return QStringLiteral("fix-orientation");
    case SPLIT_PAGES:
      return QStringLiteral("split-pages");
    case DESKEW:
      return QStringLiteral("deskew");
    case SELECT_CONTENT:
      return QStringLiteral("select-content");
    case MARGINS:
      return QStringLiteral("margins");
    case OUTPUT:
      return QStringLiteral("output");
    case EXPORT:
      return QStringLiteral("export");
  }
  return QString();
}

bool CommandLineRun::Options::parseLastStep(const QString& name, LastStep* step) {
  if (name == QLatin1String("fix-orientation")) {
    *step = FIX_ORIENTATION;
  } else if (name == QLatin1String("split-pages")) {
    *step = SPLIT_PAGES;
  } else if (name == QLatin1String("deskew")) {
    *step = DESKEW;
  } else if (name == QLatin1String("select-content")) {
    *step = SELECT_CONTENT;
  } else if (name == QLatin1String("margins")) {
    *step = MARGINS;
  } else if (name == QLatin1String("output")) {
    *step = OUTPUT;
  } else if (name == QLatin1String("export")) {
    *step = EXPORT;
  } else {
    return false;
  }
  return true;
}

CommandLineRun::ExitCode CommandLineRun::run(const Options& options,
                                             QTextStream& out,
                                             QTextStream& err,
                                             const std::atomic<bool>& cancel) {
  // ---- The usage checks of the command line ----
  const bool folderMode = !options.inputFolder.isEmpty();
  if (!folderMode) {
    // The project of a file: its values are its own; the options of the creation don't apply.
    if (options.outputFolder) {
      err << tr("The option --output-folder is only for the folder mode, which creates the project.") << Qt::endl;
      return USAGE_ERROR;
    }
    if (!options.profile.isEmpty()) {
      err << tr("The option --profile is only for the folder mode; the project knows its start values.") << Qt::endl;
      return USAGE_ERROR;
    }
    if (options.dpi) {
      err << tr("The option --dpi is only for the folder mode; the project knows the DPI of its images.") << Qt::endl;
      return USAGE_ERROR;
    }
    if (options.dpiFix) {
      err << tr("The option --dpi-fix is only for the folder mode; the project knows the DPI of its images.")
          << Qt::endl;
      return USAGE_ERROR;
    }
  } else if (options.dpi && options.dpiFix) {
    err << tr("The options --dpi and --dpi-fix exclude each other.") << Qt::endl;
    return USAGE_ERROR;
  }
  // --color-mode-detect runs from the Margins step on, the geometry only exists there.
  if (options.colorModeDetect) {
    if (!folderMode) {
      err << tr("The option --color-mode-detect is only for the folder mode.") << Qt::endl;
      return USAGE_ERROR;
    }
    if (options.lastStep < Options::MARGINS) {
      err << tr("The option --color-mode-detect needs --last-step margins or later.") << Qt::endl;
      return USAGE_ERROR;
    }
  }
  // The export options lead the run to the export; an explicit earlier last step contradicts them.
  if ((options.exportDir || options.exportName || options.formats) && (options.lastStep != Options::EXPORT)) {
    err << tr("The options --export-folder, --export-name and --export-formats need --last-step export.") << Qt::endl;
    return USAGE_ERROR;
  }
  const bool exporting = options.exports();

  // The project file: the one of the project, or the default of the new project of the folder.
  const QString projectFile
      = options.projectFile.isEmpty() ? project_files::defaultProjectFile(options.inputFolder) : options.projectFile;
  QString exportBase;
  ProcessingOptions::Formats namedFormats;
  if (!checkExportOptions(options, projectFile, &exportBase, &namedFormats, err)) {
    return USAGE_ERROR;
  }

  // The project session: of the file, or new of the folder. The read result is kept alive: the
  // reader of a project is used through the run.
  ProjectReader::FileResult read;
  const ProjectReader* reader = nullptr;
  QStringList passedOver;
  std::optional<ProjectSession> session;
  if (folderMode) {
    session = createFolderProject(options, projectFile, out, err, &passedOver);
  } else {
    read = ProjectReader::readFile(projectFile);
    if (read.error != ProjectReader::FileError::NONE) {
      err << nativePath(projectFile) << ": " << ProjectReader::errorMessage(read.error) << Qt::endl;
      return PROJECT_UNUSABLE;
    }
    reader = read.reader.get();
    session = loadExistingProject(*reader, err);
  }
  if (!session) {
    return PROJECT_UNUSABLE;
  }
  ProcessingOptions::Formats formats = namedFormats;
  ExportPaths exportPaths;
  if (exporting) {
    if (!options.formats) {
      // REVIEW-NOTE (2026-11): a project saved by 1.0.9 or 1.0.10 has no <processing-options> and no <default-params>,
      // one saved by 1.0.11 or 1.0.12 no <formats> -> the built-in defaults fill them in here, the default profile in
      // the app, so here only PDF is exported. Not fixed because no such projects are in use.
      formats = reader ? reader->readProcessingOptions(ProcessingOptions()).formats
                       : session->stages->processingOptions()->formats;
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
    if (exportReplacesProjectFile(exportPaths, formats, projectFile, *session->pages, err)) {
      return USAGE_ERROR;
    }
    // Before the processing, which may take long: a wrong name is noticed right away.
    out << tr("The export will create: %1").arg(describeOutputs(exportPaths, formats)) << Qt::endl;
  }

  const StageSequence& stages = *session->stages;
  // Loading has already recreated a deleted output folder if the project is where it was. Otherwise
  // the output would go to a folder created anew in the old place, and the images may be missing.
  if (project_files::needsRelinking(*session->pages, session->outFileNameGen.outDir())) {
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
  // The filter of the run's last step; the export runs after the Output step.
  int runLastFilterIdx = stages.outputFilterIdx();
  switch (options.lastStep) {
    case Options::FIX_ORIENTATION:
      runLastFilterIdx = stages.fixOrientationFilterIdx();
      break;
    case Options::SPLIT_PAGES:
      runLastFilterIdx = stages.pageSplitFilterIdx();
      break;
    case Options::DESKEW:
      runLastFilterIdx = stages.deskewFilterIdx();
      break;
    case Options::SELECT_CONTENT:
      runLastFilterIdx = stages.selectContentFilterIdx();
      break;
    case Options::MARGINS:
      runLastFilterIdx = stages.pageLayoutFilterIdx();
      break;
    case Options::OUTPUT:
    case Options::EXPORT:
      runLastFilterIdx = stages.outputFilterIdx();
      break;
  }
  // Without thumbnails: they are pixmaps, which need a GUI.
  const StageSequence::ProcessAllResult processed
      = stages.processAll(nullptr, session->outFileNameGen, runLastFilterIdx, progress);
  for (const StageSequence::PageFailure& failure : processed.failures) {
    err << failureMessage(failure) << Qt::endl;
  }

  // Also after a failure or cancelling: the steps stored their results for the pages done.
  if (!session->write(projectFile)) {
    err << tr("The project file could not be saved: %1").arg(nativePath(projectFile)) << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  if (processed.cancelled) {
    err << tr("Canceled. The pages output so far keep their output files.") << Qt::endl;
    return CANCELLED;
  }
  if (!processed.failures.empty()) {
    err << tr("Pages that failed: %1").arg(processed.failures.size()) << Qt::endl;
    if (exporting) {
      err << tr("Nothing was exported, as not all pages were output.") << Qt::endl;
    }
    return PAGES_FAILED;
  }
  // A run that stops before the Output step ends there by design; without failures it is a
  // success. At the Output step or later, success also means the pages got output.
  const bool needsOutput = (options.lastStep == Options::OUTPUT) || (options.lastStep == Options::EXPORT);
  if (needsOutput && !processed.output) {
    err << tr("No page was output.") << Qt::endl;
    if (exporting) {
      err << tr("Nothing was exported, as not all pages were output.") << Qt::endl;
    }
    return PAGES_FAILED;
  }
  if (needsOutput) {
    out << tr("Done. The output is in %1").arg(nativePath(session->outFileNameGen.outDir())) << Qt::endl;
  } else {
    out << tr("Done.") << Qt::endl;
  }

  if (!exporting) {
    return SUCCESS;
  }
  return exportFiles(exportPaths, formats, *session, out, err, cancel);
}