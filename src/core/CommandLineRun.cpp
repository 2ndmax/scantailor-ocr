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
#include "filters/fix_orientation/OrientationDetection.h"
#include "filters/fix_orientation/Settings.h"
#include "filters/output/ColorModeDetection.h"

namespace {
void writeProgressLine(QTextStream& json, const QString& event, QJsonObject fields);

/** The built-in profiles and their old name: "Source" is the old name of "Minimal". A profile
 * with \c listed == false is accepted by --profile but not shown by --profile-list. */
struct BuiltinProfile {
  const char* name;
  const char* canonical;
  bool listed;
};
const BuiltinProfile builtinProfiles[]
    = {{"Default", "Default", true}, {"Minimal", "Minimal", true}, {"Source", "Minimal", false}};

/** Whether \p name is the name of a built-in profile, including the old "Source". */
bool isBuiltinProfileName(const QString& name) {
  for (const BuiltinProfile& profile : builtinProfiles) {
    if (name == QLatin1String(profile.name)) {
      return true;
    }
  }
  return false;
}

/** The canonical name of a built-in profile: "Source" becomes "Minimal". Empty if not built-in. */
QString builtinProfileCanonicalName(const QString& name) {
  for (const BuiltinProfile& profile : builtinProfiles) {
    if (name == QLatin1String(profile.name)) {
      return QLatin1String(profile.canonical);
    }
  }
  return QString();
}

/** The profiles of \p folder, or of the profile folder of the program when it is empty. */
std::unique_ptr<DefaultParamsProfileManager> profileManager(const QString& folder) {
  return folder.isEmpty() ? std::make_unique<DefaultParamsProfileManager>()
                          : std::make_unique<DefaultParamsProfileManager>(folder);
}

/** The profile of the program, if it is readable; empty (null) otherwise. */
std::optional<QString> programProfileName() {
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  DefaultParamsProvider& provider = DefaultParamsProvider::getInstance();
  if (settings.getCurrentProfile() != provider.getProfileName()) {
    return std::nullopt;
  }
  return provider.getProfileName();
}

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
    const std::unique_ptr<DefaultParamsProfileManager> manager = profileManager(options.profileFolder);
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
      const QString canonical = builtinProfileCanonicalName(options.profile);
      if (!canonical.isEmpty()) {
        if (canonical == QLatin1String("Default")) {
          profile = manager->createDefaultProfile();
        } else {
          profile = manager->createMinimalProfile();
        }
      } else {
        profile = manager->readProfile(options.profile);
      }
      if (!profile) {
        err << CommandLineRun::tr("The profile %1 isn't available.").arg(options.profile) << Qt::endl;
        return std::nullopt;
      }
      profileName = canonical.isEmpty() ? options.profile : canonical;
    }
    startValues = std::move(*profile);
  } else {
    // The profile of the program: refuse it when it is unreadable.
    const std::optional<QString> programProfile = programProfileName();
    if (!programProfile) {
      err << CommandLineRun::tr(
          "The profile of the program isn't readable; pass --profile with the name or the "
          "file of one.")
          << Qt::endl;
      return std::nullopt;
    }
    startValues = DefaultParamsProvider::getInstance().getParams();
    profileName = *programProfile;
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
std::optional<ProjectSession> loadExistingProject(const ProjectReader& reader,
                                                  QTextStream& err,
                                                  ProjectSession::LoadMode mode) {
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
  return ProjectSession::load(reader, DefaultParams(), mode);
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
                                     const std::atomic<bool>& cancel,
                                     QTextStream* progressJson) {
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
  job.setCancelFlag(&cancel);
  // run() emits it in this thread, so the lambda is called directly.
  QObject::connect(&job, &ExportJob::progress, [&](const int pagesDone, const int pagesTotal) {
    out << CommandLineRun::tr("Export: %1 of %2").arg(pagesDone).arg(pagesTotal) << Qt::endl;
    if (progressJson) {
      QJsonObject exportObject;
      exportObject["done"] = pagesDone;
      exportObject["total"] = pagesTotal;
      writeProgressLine(*progressJson, QStringLiteral("export"), exportObject);
    }
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

/** The file, page and subPage of a page, as the report and the progress write them. */
QJsonObject pageIdentity(const PageInfo& page) {
  QJsonObject object;
  object["file"] = nativePath(page.imageId().filePath());
  if (page.imageId().isMultiPageFile()) {
    object["page"] = page.imageId().page();
  }
  if (page.id().subPage() != PageId::SINGLE_PAGE) {
    object["subPage"] = page.id().subPage() == PageId::LEFT_PAGE ? QStringLiteral("left") : QStringLiteral("right");
  }
  return object;
}

/** The fields of a page that --project-info and the report share: file, page, subPage, dpi,
 * dpiSource, colorMode and outputFile. */
QJsonObject basePageObject(const PageInfo& page,
                           const ProjectSession& session,
                           const QString& dpiSource,
                           const output::ColorMode colorMode) {
  QJsonObject object = pageIdentity(page);
  object["dpi"] = dpiObject(page.metadata().dpi());
  object["dpiSource"] = dpiSource;
  object["colorMode"] = colorModeName(colorMode);
  object["outputFile"] = nativePath(session.outFileNameGen.filePathFor(page.id()));
  return object;
}

/** The native paths of \p paths as a JSON array, shared by the report and the progress. */
QJsonArray nativePaths(const QStringList& paths) {
  QJsonArray array;
  for (const QString& path : paths) {
    array.append(nativePath(path));
  }
  return array;
}

/** Writes one compact JSON line of the progress output. */
void writeProgressLine(QTextStream& json, const QString& event, QJsonObject fields) {
  fields["schemaVersion"] = 1;
  fields["event"] = event;
  json << QJsonDocument(fields).toJson(QJsonDocument::Compact) << Qt::endl;
}

/** The filter index the run stops at for \p step. */
int filterIndexForLastStep(const StageSequence& stages, CommandLineRun::Options::LastStep step) {
  switch (step) {
    case CommandLineRun::Options::FIX_ORIENTATION:
      return stages.fixOrientationFilterIdx();
    case CommandLineRun::Options::SPLIT_PAGES:
      return stages.pageSplitFilterIdx();
    case CommandLineRun::Options::DESKEW:
      return stages.deskewFilterIdx();
    case CommandLineRun::Options::SELECT_CONTENT:
      return stages.selectContentFilterIdx();
    case CommandLineRun::Options::MARGINS:
      return stages.pageLayoutFilterIdx();
    case CommandLineRun::Options::OUTPUT:
    case CommandLineRun::Options::EXPORT:
      return stages.outputFilterIdx();
  }
  return stages.outputFilterIdx();
}

/** The name of the step a filter index stands for, or an empty string. */
QString stepNameForFilterIndex(const StageSequence& stages, const int filterIdx) {
  for (int step = 0; step <= CommandLineRun::Options::EXPORT; ++step) {
    if (filterIndexForLastStep(stages, static_cast<CommandLineRun::Options::LastStep>(step)) == filterIdx) {
      return CommandLineRun::Options::stepName(static_cast<CommandLineRun::Options::LastStep>(step));
    }
  }
  return QString();
}

/**
 * Writes the report of the run to \p reportFile: the schema is the one in the doc of
 * Options::reportFile. Returns false after telling \p err why it couldn't be written.
 */
bool writeReportFile(
    const QString& reportFile,
    const QString& projectFile,
    const QString& usedProfile,
    const CommandLineRun::Options& options,
    const CommandLineRun::ExitCode exitCode,
    const bool cancelled,
    const ProjectSession& session,
    const QStringList& passedOver,
    const std::vector<std::pair<PageId, output::ColorMode>>& detectedPages,
    const std::vector<std::pair<ImageId, fix_orientation::OrientationDetection::PageResult>>& detectedOrientations,
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
  std::map<ImageId, fix_orientation::OrientationDetection::PageResult> detectedOrientation;
  for (const std::pair<ImageId, fix_orientation::OrientationDetection::PageResult>& entry : detectedOrientations) {
    detectedOrientation.emplace(entry.first, entry.second);
  }

  QJsonArray pages;
  for (const PageInfo& page : session.pages->toPageSequence(PAGE_VIEW)) {
    const QFileInfo file(page.imageId().filePath());

    const QString dpiSource = (options.dpi || fixedFiles.contains(file.absoluteFilePath()))
                                  ? QStringLiteral("command-line")
                                  : QStringLiteral("file");
    // Without detection, the page keeps the mode it has in the project. The flag says which of the
    // two the report shows.
    const auto detectedPage = detected.find(pageKey(page.id()));
    const bool colorModeDetected = detectedPage != detected.end();
    const output::ColorMode mode
        = detectedPage != detected.end() ? detectedPage->second : session.stages->outputFilter()->colorMode(page.id());
    QJsonObject pageObject = basePageObject(page, session, dpiSource, mode);
    pageObject["colorModeDetected"] = colorModeDetected;
    pageObject["rotation"]
        = session.stages->fixOrientationFilter()->settings()->getRotationFor(page.imageId()).toDegrees();
    if (!options.orientationDetect.isEmpty()) {
      const auto detectedOrientationIt = detectedOrientation.find(page.imageId());
      if (detectedOrientationIt != detectedOrientation.end()) {
        const fix_orientation::OrientationDetection::PageResult::Status status = detectedOrientationIt->second.status;
        QString value;
        switch (status) {
          case fix_orientation::OrientationDetection::PageResult::DETECTED:
            value = QStringLiteral("detected");
            break;
          case fix_orientation::OrientationDetection::PageResult::BOOK_RULE:
            value = QStringLiteral("book-rule");
            break;
          case fix_orientation::OrientationDetection::PageResult::UNDECIDED:
            value = QStringLiteral("undecided");
            break;
          case fix_orientation::OrientationDetection::PageResult::FAILED:
            value = QStringLiteral("failed");
            break;
        }
        pageObject["rotationDetection"] = value;
      }
    }
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

  const QJsonArray passedOverFiles = nativePaths(passedOver);

  QJsonObject report;
  report["schemaVersion"] = 1;
  report["run"] = run;
  report["pages"] = pages;
  report["exports"] = exports;
  report["passedOver"] = passedOverFiles;

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

/** Whether the project must be relinked before it can be processed; a deleted output folder is
 * acceptable when the project is still in place, because a run recreates it. */
bool projectNeedsRelinking(const ProjectSession& session) {
  const ProjectPages& pages = *session.pages;
  const QString outDir = session.outFileNameGen.outDir();
  if (!project_files::allImagesExist(pages)) {
    return true;
  }
  if (!QDir(outDir).exists()) {
    return !project_files::isProjectInPlace(pages, outDir, QString());
  }
  return false;
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
                                             const std::atomic<bool>& cancel,
                                             QTextStream* progressJson) {
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
  // --orientation-detect runs in the folder mode, before the first step.
  if (!options.orientationDetect.isEmpty()) {
    if (!folderMode) {
      err << tr("The option --orientation-detect is only for the folder mode.") << Qt::endl;
      return USAGE_ERROR;
    }
    if (options.orientationDetect != QStringLiteral("pages") && options.orientationDetect != QStringLiteral("book")) {
      err << tr("Unknown method after --orientation-detect: %1. The methods are: pages and book.")
                 .arg(options.orientationDetect)
          << Qt::endl;
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
  QString usedProfile;
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
    session = loadExistingProject(*reader, err, ProjectSession::LoadMode::Normal);
  }
  if (!session) {
    return PROJECT_UNUSABLE;
  }
  // The output folder takes the output and the cache of the pages; one that takes no files would
  // fail every page. A missing one of a project is left to the relinking check below, which tells
  // what to do; a missing one of a new project is created during processing.
  // REVIEW-NOTE (2026-11): an existing output folder that takes no new files -> refused here. Not
  // tested because a test can't make a folder unwritable on Windows without changing its access
  // rights; path_names::isWritableDir, which decides it, is tested on its own.
  {
    const QString outDir = session->outFileNameGen.outDir();
    if (QFileInfo(outDir).isDir() ? !path_names::isWritableDir(outDir)
                                  : (folderMode && !path_names::canCreateOrWriteDir(outDir))) {
      err << tr("The output folder can't be written to: %1").arg(nativePath(outDir)) << Qt::endl;
      return PROJECT_UNUSABLE;
    }
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
    // The languages text recognition needs are known from the project; setting it up, which may
    // copy language files, waits for the export.
    const ProcessingOptions& project = *session->stages->processingOptions();
    if (formats.needsTextRecognition(project.pdf.textLayer) && !checkOcrLanguages(project.ocr.languages, err)) {
      return PROJECT_UNUSABLE;
    }
    // Before the processing, which may take long: a wrong name is noticed right away.
    out << tr("The export will create: %1").arg(describeOutputs(exportPaths, formats)) << Qt::endl;
  }

  const StageSequence& stages = *session->stages;
  // A deleted output folder is acceptable when the project is still in place, because a run recreates
  // it; projectNeedsRelinking covers that and the missing-image case.
  if (projectNeedsRelinking(*session)) {
    err << tr(
        "The output folder or some images of the project are missing, for example because the project was "
        "moved. Open it in ScanTailor OCR, relink it there and save it, then it can be processed here.")
        << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  int lastProgressPassFilterIdx = -1;
  const StageSequence::ProgressCallback progress = [&](const StageSequence::PageProgress& p) {
    out << tr("%1: %2 of %3").arg(stages.filterAt(p.lastFilterIdx)->getName()).arg(p.done).arg(p.total) << Qt::endl;
    if (progressJson) {
      const QString step = stepNameForFilterIndex(stages, p.lastFilterIdx);
      if (p.lastFilterIdx != lastProgressPassFilterIdx) {
        lastProgressPassFilterIdx = p.lastFilterIdx;
        QJsonObject passStart;
        passStart["step"] = step;
        passStart["total"] = p.total;
        writeProgressLine(*progressJson, QStringLiteral("pass-start"), passStart);
      }
      QJsonObject pageObj = pageIdentity(p.page);
      pageObj["step"] = step;
      pageObj["done"] = p.done;
      pageObj["total"] = p.total;
      if (p.status == StageSequence::PageProgress::OK) {
        pageObj["status"] = QStringLiteral("ok");
      } else if (p.status == StageSequence::PageProgress::FAILED) {
        pageObj["status"] = QStringLiteral("failed");
        pageObj["errorKind"] = errorKindName(p.failure.kind);
        pageObj["reasons"] = QJsonArray::fromStringList(p.failure.reasons);
      } else {
        pageObj["status"] = QStringLiteral("skipped");
      }
      if (step == QStringLiteral("output")) {
        pageObj["outputFile"] = nativePath(session->outFileNameGen.filePathFor(p.page.id()));
      }
      writeProgressLine(*progressJson, QStringLiteral("page"), pageObj);
    }
    return !cancel;
  };
  // The filter of the run's last step; the export runs after the Output step.
  const int runLastFilterIdx = filterIndexForLastStep(stages, options.lastStep);

  // --orientation-detect: before the first step, in the folder mode.
  std::vector<std::pair<ImageId, fix_orientation::OrientationDetection::PageResult>> detectedOrientations;
  bool orientationDetectionCancelled = false;
  if (!options.orientationDetect.isEmpty()) {
    const bool bookRule = (options.orientationDetect == QStringLiteral("book"));
    std::vector<PageInfo> pages;
    for (const PageInfo& page : session->pages->toPageSequence(IMAGE_VIEW)) {
      pages.push_back(page);
    }
    std::set<PageId> selection;
    for (const PageInfo& page : pages) {
      selection.insert(page.id());
    }
    if (progressJson) {
      QJsonObject passStart;
      passStart["step"] = QStringLiteral("orientation-detect");
      passStart["total"] = static_cast<int>(pages.size());
      writeProgressLine(*progressJson, QStringLiteral("pass-start"), passStart);
    }
    int orientationDone = 0;
    const fix_orientation::OrientationDetection::Result result = fix_orientation::OrientationDetection::run(
        stages.fixOrientationFilter()->settings(), pages, selection, bookRule, NullTaskStatus(),
        [&cancel](const int) { return !cancel; },
        [&](const PageInfo& page, const fix_orientation::OrientationDetection::PageResult& pr) {
          detectedOrientations.emplace_back(page.imageId(), pr);
          if (progressJson) {
            ++orientationDone;
            QJsonObject pageObj = pageIdentity(page);
            pageObj["step"] = QStringLiteral("orientation-detect");
            pageObj["done"] = orientationDone;
            pageObj["total"] = static_cast<int>(pages.size());
            if (pr.status == fix_orientation::OrientationDetection::PageResult::DETECTED
                || pr.status == fix_orientation::OrientationDetection::PageResult::BOOK_RULE) {
              pageObj["status"] = QStringLiteral("ok");
              pageObj["rotation"] = pr.angle;
              pageObj["rotationDetection"] = (pr.status == fix_orientation::OrientationDetection::PageResult::DETECTED)
                                                 ? QStringLiteral("detected")
                                                 : QStringLiteral("book-rule");
            } else if (pr.status == fix_orientation::OrientationDetection::PageResult::UNDECIDED) {
              pageObj["status"] = QStringLiteral("uncertain");
            } else {
              pageObj["status"] = QStringLiteral("failed");
            }
            writeProgressLine(*progressJson, QStringLiteral("page"), pageObj);
          }
        });
    orientationDetectionCancelled = result.cancelled;

    // Summary on stdout, shared with the app.
    out << fix_orientation::OrientationDetection::summary(result, selection, bookRule, [](const PageId& pageId) {
      return pageName(pageId);
    }) << Qt::endl;
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
      if (progressJson) {
        QJsonObject passStart;
        passStart["step"] = QStringLiteral("color-mode-detect");
        passStart["total"] = static_cast<int>(pages.size());
        writeProgressLine(*progressJson, QStringLiteral("pass-start"), passStart);
      }
      int colorModeDone = 0;
      const output::ColorModeDetection::Result result = output::ColorModeDetection::run(
          stages.outputFilter(), pages, geometryTask, status, [&cancel](const int) { return !cancel; },
          [&](const PageInfo& page, const output::ColorModeDetection::PageResult& pr) {
            if (!progressJson) {
              return;
            }
            ++colorModeDone;
            QJsonObject pageObj = pageIdentity(page);
            pageObj["step"] = QStringLiteral("color-mode-detect");
            pageObj["done"] = colorModeDone;
            pageObj["total"] = static_cast<int>(pages.size());
            if (pr.status == output::ColorModeDetection::PageResult::DETECTED) {
              pageObj["status"] = QStringLiteral("ok");
              pageObj["colorMode"] = colorModeName(pr.mode);
            } else if (pr.status == output::ColorModeDetection::PageResult::SKIPPED) {
              pageObj["status"] = QStringLiteral("skipped");
            } else {
              pageObj["status"] = QStringLiteral("failed");
              pageObj["errorKind"] = QStringLiteral("LOAD_FAILED");
            }
            writeProgressLine(*progressJson, QStringLiteral("page"), pageObj);
          });
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
  const bool cancelled = processed.cancelled || detectionCancelled || orientationDetectionCancelled;

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
        exitCode = exportFiles(exportPaths, formats, *session, out, err, cancel, progressJson);
        if (exitCode == SUCCESS) {
          exportedFiles = selectedOutputs(exportPaths, formats);
        }
      }
    } else {
      exitCode = SUCCESS;
    }
  }
  if (options.reportFile) {
    // cancelled follows the exit code: any cancellation, including one during the export, is exit 5.
    writeReportFile(*options.reportFile, projectFile, usedProfile, options, *exitCode, *exitCode == CANCELLED, *session,
                    passedOver, detectedPages, detectedOrientations, exportedFiles, processed.failures, fixedFiles,
                    err);
  }
  // Exit codes 1 and 2 return earlier, and the late PROJECT_UNUSABLE from saving the project returns
  // before this point, so only 0, 3, 4 and 5 reach here.
  if (progressJson) {
    QJsonObject runEnd;
    runEnd["exitCode"] = static_cast<int>(*exitCode);
    runEnd["exports"] = QJsonArray::fromStringList(exportedFiles);
    runEnd["passedOver"] = nativePaths(passedOver);
    writeProgressLine(*progressJson, QStringLiteral("run-end"), runEnd);
  }
  return *exitCode;
}

CommandLineRun::ExitCode CommandLineRun::profileList(QTextStream& out, QTextStream& err, const QString& profileDir) {
  QJsonObject root;
  root["schemaVersion"] = 1;

  // The profile a run without --profile takes, only when it is readable.
  const std::optional<QString> programProfile = programProfileName();
  if (programProfile) {
    root["profile"] = *programProfile;
  }

  QJsonArray profiles;
  // Built-in profiles first.
  for (const BuiltinProfile& builtin : builtinProfiles) {
    if (!builtin.listed) {
      continue;
    }
    QJsonObject entry;
    entry["name"] = QLatin1String(builtin.name);
    entry["source"] = QStringLiteral("built-in");
    profiles.append(entry);
  }

  // File profiles, sorted by name; skip names shadowed by the built-ins and unreadable files.
  const std::unique_ptr<DefaultParamsProfileManager> manager = profileManager(profileDir);
  std::list<QString> fileNames = manager->getProfileList();
  std::vector<QString> sorted(fileNames.begin(), fileNames.end());
  std::sort(sorted.begin(), sorted.end());
  for (const QString& name : sorted) {
    if (isBuiltinProfileName(name)) {
      continue;
    }
    if (!manager->readProfile(name)) {
      continue;
    }
    QJsonObject entry;
    entry["name"] = name;
    entry["source"] = QStringLiteral("file");
    profiles.append(entry);
  }
  root["profiles"] = profiles;

  out << QJsonDocument(root).toJson(QJsonDocument::Indented);
  return SUCCESS;
}

CommandLineRun::ExitCode CommandLineRun::projectInfo(const QString& projectFile, QTextStream& out, QTextStream& err) {
  // Read like a run.
  ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  if (read.error != ProjectReader::FileError::NONE) {
    err << nativePath(projectFile) << ": " << ProjectReader::errorMessage(read.error) << Qt::endl;
    return PROJECT_UNUSABLE;
  }
  std::optional<ProjectSession> session
      = loadExistingProject(*read.reader, err, ProjectSession::LoadMode::WithoutSideEffects);
  if (!session) {
    return PROJECT_UNUSABLE;
  }
  // Relink check with deleted output folder accepted when in place.
  if (projectNeedsRelinking(*session)) {
    err << tr(
        "The output folder or some images of the project are missing, for example because the project was "
        "moved. Open it in ScanTailor OCR, relink it there and save it, then it can be processed here.")
        << Qt::endl;
    return PROJECT_UNUSABLE;
  }

  QJsonObject root;
  root["schemaVersion"] = 1;
  root["mode"] = QStringLiteral("project");
  root["projectFile"] = nativePath(projectFile);
  root["outputFolder"] = nativePath(session->outFileNameGen.outDir());

  QJsonArray pages;
  for (const PageInfo& page : session->pages->toPageSequence(PAGE_VIEW)) {
    const output::ColorMode mode = session->stages->outputFilter()->colorMode(page.id());
    pages.append(basePageObject(page, *session, QStringLiteral("file"), mode));
  }
  root["pages"] = pages;

  out << QJsonDocument(root).toJson(QJsonDocument::Indented);
  return SUCCESS;
}