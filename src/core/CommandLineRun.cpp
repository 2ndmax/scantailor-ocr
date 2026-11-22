// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CommandLineRun.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include <ApplicationSettings.h>
#include <CompositeCacheDrivenTask.h>
#include <CoreUtils.h>
#include <DefaultParamsProfileManager.h>
#include <DefaultParamsProvider.h>
#include <Dpi.h>
#include <NullTaskStatus.h>
#include <PageView.h>
#include <SmartFilenameOrdering.h>

#include <QFile>

#include "DefaultParams.h"
#include "ExportJob.h"
#include "ImageFileInfo.h"
#include "OcrLanguages.h"
#include "PageSequence.h"
#include "PathNames.h"
#include "ProcessingOptions.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectSession.h"
#include "StageSequence.h"
#include "ThreadBudget.h"
#include "filters/output/ColorModeDetection.h"

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

/**
 * The project is saved at the end of a run, which may take hours; this tells at the start that it
 * couldn't be.
 */
QString projectNotSavableMessage(const QString& projectFile) {
  return CommandLineRun::tr(
             "The project file can't be saved: %1. Its folder must take new files, and the file must "
             "not be read-only.")
      .arg(nativePath(projectFile));
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
  QFileInfoList images = project_files::importableImagesIn(inputDir);
  // The app's Project Files dialog also lists hidden files, so the shared helper keeps them. They
  // are no scans here, and neither is the output of the default output folder in the input folder.
  images.erase(std::remove_if(images.begin(), images.end(), [](const QFileInfo& file) { return file.isHidden(); }),
               images.end());
  std::sort(images.begin(), images.end(), SmartFilenameOrdering());
  QSet<QString> usablePaths;
  for (const QFileInfo& file : images) {
    usablePaths.insert(file.filePath());
  }

  bool failed = false;
  for (const QFileInfo& file : images) {
    QStringList loadErrors;
    QStringList partialReads;
    std::optional<ImageFileInfo> loaded = ImageFileInfo::load(file, &loadErrors, &partialReads);
    if (!loaded.has_value()) {
      for (const QString& reason : loadErrors) {
        err << file.filePath() << ": " << reason << Qt::endl;
      }
      // All bad files are named, not only the first one the folder happens to list.
      failed = true;
      continue;
    }
    if (!loaded->partialReads().isEmpty()) {
      err << file.filePath() << ": " << CommandLineRun::tr("The file is only partly readable.") << Qt::endl;
      for (const QString& reason : loaded->partialReads()) {
        err << "  " << reason << Qt::endl;
      }
      failed = true;
      continue;
    }
    files->push_back(std::move(*loaded));
  }
  QFileInfoList all(QDir(inputDir).entryInfoList(QDir::Files));
  std::sort(all.begin(), all.end(), SmartFilenameOrdering());
  for (const QFileInfo& file : all) {
    if (!usablePaths.contains(file.filePath())) {
      passedOver->append(file.filePath());
    }
  }
  if (failed) {
    return false;
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
                                                  QString* profileOut,
                                                  QStringList* passedOver,
                                                  QStringList* fixedFilesOut) {
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
  if (!path_names::canWriteFile(projectFile)) {
    err << projectNotSavableMessage(projectFile) << Qt::endl;
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
  // The report names the profile that was used, not the spelling of --profile.
  *profileOut = profileName;

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

  // Create the folder only now that the project is known to be usable; a refused one leaves no
  // empty folder behind.
  if (options.outputFolder && !QDir(outDir).mkpath(outDir)) {
    err << CommandLineRun::tr("Unable to create the output folder: %1").arg(nativePath(outDir)) << Qt::endl;
    return std::nullopt;
  }

  // The project: its pages take the files, whose DPI is set; its start values and page order
  // the profile.
  if (fixedFilesOut != nullptr) {
    *fixedFilesOut = fixedFiles;
  }
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
  // Without --export-folder it is the folder of the project file. If that is missing, reading the
  // project fails, which is told with the right exit code. A folder named on the command line must
  // take the files or be possible to create, which is checked now, before a long run; it is only
  // created once the project and the pages are known to be usable.
  if (options.exportDir && !path_names::canCreateOrWriteDir(folder)) {
    if (QFileInfo(folder).isDir()) {
      err << CommandLineRun::tr("The export folder can't be written to: %1").arg(nativePath(folder)) << Qt::endl;
    } else {
      err << CommandLineRun::tr("Unable to create the export folder: %1").arg(nativePath(folder)) << Qt::endl;
    }
    return false;
  }
  *exportBase = QDir(folder).filePath(name);
  return true;
}

/** The files the chosen formats create, for telling the user and for the report. */
QStringList selectedOutputs(const ExportPaths& paths, const ProcessingOptions::Formats& formats) {
  QStringList outputs;
  for (const auto& [chosen, path] : {std::pair(formats.pdf, paths.pdf), std::pair(formats.txt, paths.txt),
                                     std::pair(formats.hocr, paths.hocr), std::pair(formats.alto, paths.altoDir)}) {
    if (chosen) {
      outputs.push_back(nativePath(path));
    }
  }
  return outputs;
}

/** What the export of \p formats creates, for telling the user. */
QString describeOutputs(const ExportPaths& paths, const ProcessingOptions::Formats& formats) {
  return selectedOutputs(paths, formats).join(QStringLiteral(", "));
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

QString noOcrLanguageMessage() {
  return CommandLineRun::tr(
      "The export needs text recognition, but the project has no language ticked. Tick one in "
      "ScanTailor OCR, in the Export step.");
}

/**
 * Whether text recognition can run with the languages of the project: they are installed, and
 * there is one. The app asks about languages that aren't installed and offers to download them;
 * here, they end the run. Sets nothing up. Returns false after telling \p err why not.
 */
bool checkOcrLanguages(const QStringList& languages, QTextStream& err) {
  const QStringList missing = OcrLanguages::missing(languages);
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
  if (languages.isEmpty()) {
    err << noOcrLanguageMessage() << Qt::endl;
    return false;
  }
  return true;
}

/**
 * Sets up the text recognition as the formats need it. Returns false after telling \p err why it
 * can't be done.
 */
bool setUpOcr(const ProcessingOptions& project, ExportOptions* options, QTextStream& err) {
  if (!options->formats.needsTextRecognition(options->pdf.textLayer)) {
    return true;
  }
  if (!checkOcrLanguages(project.ocr.languages, err)) {
    return false;
  }
  QString error;
  switch (options->setUpOcr(project.ocr.languages, project.ocr.pageLayout, &error)) {
    case ExportOptions::OcrSetup::DONE:
      return true;
    case ExportOptions::OcrSetup::NO_LANGUAGE:
      err << noOcrLanguageMessage() << Qt::endl;
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

/** The name of a color mode, as the report of --report-file writes it. */
QString colorModeName(const output::ColorMode mode) {
  switch (mode) {
    case output::BLACK_AND_WHITE:
      return QStringLiteral("black-and-white");
    case output::COLOR_GRAYSCALE:
      return QStringLiteral("color-grayscale");
    case output::MIXED:
      return QStringLiteral("mixed");
  }
  return QString();
}

/** The name of the kind of a failed page, as the report of --report-file writes it. */
QString errorKindName(const StageSequence::PageFailure::Kind kind) {
  switch (kind) {
    case StageSequence::PageFailure::LOAD_FAILED:
      return QStringLiteral("LOAD_FAILED");
    case StageSequence::PageFailure::PROCESSING_FAILED:
      return QStringLiteral("PROCESSING_FAILED");
    case StageSequence::PageFailure::OUTPUT_TOO_LARGE:
      return QStringLiteral("OUTPUT_TOO_LARGE");
    case StageSequence::PageFailure::OUTPUT_NOT_WRITTEN:
      return QStringLiteral("OUTPUT_NOT_WRITTEN");
  }
  return QString();
}

/** The key of a page in the report: the file, the page of the file and the half of a split page. */
QString pageKey(const PageId& page) {
  return page.imageId().filePath() + QLatin1Char('#') + QString::number(page.imageId().page()) + QLatin1Char('#')
         + QString::number(static_cast<int>(page.subPage()));
}

/** The object with the two axes of a DPI. */
QJsonObject dpiObject(const Dpi& dpi) {
  QJsonObject object;
  object["horizontal"] = dpi.horizontal();
  object["vertical"] = dpi.vertical();
  return object;
}

/**
 * Writes the report of the run to \p reportFile: the schema is the one in the doc of
 * Options::reportFile. Returns false after telling \p err why it couldn't be written.
 */
bool writeReportFile(const QString& reportFile,
                     const QString& projectFile,
                     const QString& usedProfile,
                     const CommandLineRun::Options& options,
                     const CommandLineRun::ExitCode exitCode,
                     const bool cancelled,
                     const ProjectSession& session,
                     const QStringList& passedOver,
                     const std::vector<std::pair<PageId, output::ColorMode>>& detectedPages,
                     const QStringList& exportedFiles,
                     const std::vector<StageSequence::PageFailure>& pageFailures,
                     const QStringList& fixedFiles,
                     QTextStream& err) {
  std::map<QString, const StageSequence::PageFailure*> failuresByPage;
  for (const StageSequence::PageFailure& failure : pageFailures) {
    failuresByPage.emplace(pageKey(failure.page), &failure);
  }
  std::map<QString, output::ColorMode> detected;
  for (const std::pair<PageId, output::ColorMode>& entry : detectedPages) {
    detected.emplace(pageKey(entry.first), entry.second);
  }

  QJsonArray pages;
  for (const PageInfo& page : session.pages->toPageSequence(PAGE_VIEW)) {
    const QFileInfo file(page.imageId().filePath());
    QJsonObject pageObject;
    pageObject["file"] = file.filePath();
    if (page.imageId().isMultiPageFile()) {
      pageObject["page"] = page.imageId().page();
    }
    if (page.id().subPage() != PageId::SINGLE_PAGE) {
      pageObject["subPage"]
          = page.id().subPage() == PageId::LEFT_PAGE ? QStringLiteral("left") : QStringLiteral("right");
    }
    pageObject["dpi"] = dpiObject(page.metadata().dpi());
    pageObject["dpiSource"] = (options.dpi || fixedFiles.contains(file.absoluteFilePath()))
                                  ? QStringLiteral("command-line")
                                  : QStringLiteral("file");
    // Without detection, the page keeps the mode it has in the project. The flag says which of the
    // two the report shows.
    const auto detectedPage = detected.find(pageKey(page.id()));
    pageObject["colorModeDetected"] = detectedPage != detected.end();
    const output::ColorMode mode
        = detectedPage != detected.end() ? detectedPage->second : session.stages->outputFilter()->colorMode(page.id());
    pageObject["colorMode"] = colorModeName(mode);
    pageObject["outputFile"] = session.outFileNameGen.filePathFor(page.id());
    const auto failure = failuresByPage.find(pageKey(page.id()));
    if (failure != failuresByPage.end()) {
      pageObject["status"] = QStringLiteral("failed");
      pageObject["errorKind"] = errorKindName(failure->second->kind);
      pageObject["reasons"] = QJsonArray::fromStringList(failure->second->reasons);
    } else {
      pageObject["status"] = QStringLiteral("ok");
    }
    pages.append(pageObject);
  }

  QJsonArray exports = QJsonArray::fromStringList(exportedFiles);

  QJsonObject run;
  run["mode"] = options.inputFolder.isEmpty() ? QStringLiteral("project") : QStringLiteral("folder");
  run["projectFile"] = nativePath(projectFile);
  run["profile"] = usedProfile;
  run["lastStep"] = CommandLineRun::Options::stepName(options.lastStep);
  run["exitCode"] = static_cast<int>(exitCode);
  run["cancelled"] = cancelled;

  QJsonObject report;
  report["schemaVersion"] = 1;
  report["run"] = run;
  report["pages"] = pages;
  report["exports"] = exports;
  report["passedOver"] = QJsonArray::fromStringList(passedOver);

  QFile file(reportFile);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    err << CommandLineRun::tr("The report file could not be written: %1").arg(nativePath(reportFile)) << Qt::endl;
    return false;
  }
  QTextStream stream(&file);
  stream << QJsonDocument(report).toJson(QJsonDocument::Indented);
  file.close();
  return true;
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
  // REVIEW-NOTE (2026-11): a usage error or an unusable project returns before the report is written
  // -> exit codes 1 and 2 have no report. Not fixed because the spec leaves this open; a report with
  // only "run" would give scripts one artifact for every exit.
  // The folder of the report file must exist; the tool writes the report into it at the end, so
  // that it must be possible is checked now, before a long run.
  if (options.reportFile) {
    const QFileInfo report(*options.reportFile);
    if (!report.absoluteDir().exists()) {
      err << tr("The folder of the report file doesn't exist: %1").arg(nativePath(report.absolutePath())) << Qt::endl;
      return USAGE_ERROR;
    }
    if (!path_names::canWriteFile(*options.reportFile)) {
      err << tr("The report file could not be written: %1").arg(nativePath(*options.reportFile)) << Qt::endl;
      return USAGE_ERROR;
    }
  }
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
  // --threads: the budget of this run; the setting of the program stays as it is.
  if (options.threads && (*options.threads < 1)) {
    err << tr("The number of threads after --threads is too small; it must be at least 1.") << Qt::endl;
    return USAGE_ERROR;
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
  // Only now that all usage checks passed does the budget of this run replace the program's one.
  if (options.threads) {
    ThreadBudget::setMaxThreads(*options.threads);
  }

  // The project session: of the file, or new of the folder. The read result is kept alive: the
  // reader of a project is used through the run.
  ProjectReader::FileResult read;
  const ProjectReader* reader = nullptr;
  QStringList passedOver;
  QStringList fixedFiles;
  QString usedProfile = DefaultParamsProvider::getInstance().getProfileName();
  std::optional<ProjectSession> session;
  if (folderMode) {
    session = createFolderProject(options, projectFile, out, err, &usedProfile, &passedOver, &fixedFiles);
  } else {
    read = ProjectReader::readFile(projectFile);
    if (read.error != ProjectReader::FileError::NONE) {
      err << nativePath(projectFile) << ": " << ProjectReader::errorMessage(read.error) << Qt::endl;
      return PROJECT_UNUSABLE;
    }
    if (!path_names::canWriteFile(projectFile)) {
      err << projectNotSavableMessage(projectFile) << Qt::endl;
      return PROJECT_UNUSABLE;
    }
    reader = read.reader.get();
    session = loadExistingProject(*reader, err);
  }
  if (!session) {
    return PROJECT_UNUSABLE;
  }
  if (folderMode && !passedOver.isEmpty()) {
    out << tr("%n other file(s) passed over.", nullptr, passedOver.size()) << Qt::endl;
  }
  ProcessingOptions::Formats formats = namedFormats;
  ExportPaths exportPaths;
  QStringList exportedFiles;
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
  // --color-mode-detect: as the app's Detect Color Mode, between the Margins and the Output passes.
  // The hook also runs when the last step is Margins, where there is no Output pass.
  std::vector<std::pair<PageId, output::ColorMode>> detectedPages;
  bool detectionCancelled = false;
  StageSequence::AfterMarginsCallback afterMargins;
  if (options.colorModeDetect) {
    afterMargins = [&] {
      std::vector<PageInfo> pages;
      for (const PageInfo& page : session->pages->toPageSequence(PAGE_VIEW)) {
        pages.push_back(page);
      }
      const std::shared_ptr<CompositeCacheDrivenTask> geometryTask(
          stages.createCacheDrivenTask(stages.outputFilterIdx(), session->outFileNameGen));
      const NullTaskStatus status;
      const output::ColorModeDetection::Result result = output::ColorModeDetection::run(
          stages.outputFilter(), pages, geometryTask, status, [&cancel](const int) { return !cancel; });
      detectedPages = result.detected;
      detectionCancelled = result.cancelled;
      QString summary = tr("Color mode detected for %n page(s): %1 black and white, %2 color / grayscale, %3 mixed.",
                           nullptr, result.blackAndWhite + result.colorGrayscale + result.mixed)
                            .arg(result.blackAndWhite)
                            .arg(result.colorGrayscale)
                            .arg(result.mixed);
      if (result.skipped > 0) {
        summary += "\n\n"
                   + tr("%n page(s) skipped because the steps before the detection have no result yet.", nullptr,
                        result.skipped);
      }
      if (result.failed > 0) {
        summary += "\n\n" + tr("%n page(s) could not be loaded.", nullptr, result.failed);
      }
      if (result.cancelled) {
        summary += "\n\n" + tr("The detection was canceled.");
      }
      out << summary << Qt::endl;
    };
  }

  // Without thumbnails: they are pixmaps, which need a GUI.
  const StageSequence::ProcessAllResult processed
      = stages.processAll(nullptr, session->outFileNameGen, runLastFilterIdx, progress, afterMargins);
  for (const StageSequence::PageFailure& failure : processed.failures) {
    err << failureMessage(failure) << Qt::endl;
  }
  const bool cancelled = processed.cancelled || detectionCancelled;

  // Also after a failure or cancelling: the steps stored their results for the pages done.
  if (!session->write(projectFile)) {
    err << tr("The project file could not be saved: %1").arg(nativePath(projectFile)) << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  // The exit code of the run; the report file is written with it, also when pages failed or the
  // run was cancelled.
  std::optional<ExitCode> exitCode;
  if (cancelled) {
    err << tr("Canceled. The pages output so far keep their output files.") << Qt::endl;
    exitCode = CANCELLED;
  }
  if (!processed.failures.empty()) {
    err << tr("Pages that failed: %1").arg(processed.failures.size()) << Qt::endl;
    if (exporting) {
      err << tr("Nothing was exported, as not all pages were output.") << Qt::endl;
    }
    if (!exitCode) {
      exitCode = PAGES_FAILED;
    }
  }
  // A run that stops before the Output step ends there by design; without failures it is a
  // success. At the Output step or later, success also means the pages got output.
  const bool needsOutput = (options.lastStep == Options::OUTPUT) || (options.lastStep == Options::EXPORT);
  if (needsOutput && !processed.output && !exitCode) {
    err << tr("No page was output.") << Qt::endl;
    if (exporting) {
      err << tr("Nothing was exported, as not all pages were output.") << Qt::endl;
    }
    exitCode = PAGES_FAILED;
  }
  if (!exitCode) {
    if (needsOutput) {
      out << tr("Done. The output is in %1").arg(nativePath(session->outFileNameGen.outDir())) << Qt::endl;
    } else {
      out << tr("Done.") << Qt::endl;
    }
    if (exporting) {
      // The folder was checked at the start; this only fails if it changed during the run. The
      // pages are processed by then, so it is a failed export, told in the report too.
      // Fixed without a test: a failure here returned exit code 1 and wrote no report; no test can
      // make a folder that passed the check at the start fail to be created at the end.
      const QString folder = QFileInfo(exportBase).absolutePath();
      if (options.exportDir && !QFileInfo(folder).isDir() && !QDir().mkpath(folder)) {
        err << tr("Unable to create the export folder: %1").arg(nativePath(folder)) << Qt::endl;
        exitCode = EXPORT_FAILED;
      } else {
        exitCode = exportFiles(exportPaths, formats, *session, out, err, cancel);
        if (exitCode == SUCCESS) {
          exportedFiles = selectedOutputs(exportPaths, formats);
        }
      }
    } else {
      exitCode = SUCCESS;
    }
  }
  if (options.reportFile) {
    writeReportFile(*options.reportFile, projectFile, usedProfile, options, *exitCode, cancelled, *session, passedOver,
                    detectedPages, exportedFiles, processed.failures, fixedFiles, err);
  }
  return *exitCode;
}