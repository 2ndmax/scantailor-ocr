// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROJECTFILES_H_
#define SCANTAILOR_CORE_PROJECTFILES_H_

#include <QFileInfo>
#include <QFileInfoList>
#include <QString>

class ProjectPages;

/** Naming rules for project files and the images a project can be made of. */
namespace project_files {
/** The pattern of project files for file dialogs, e.g. "*.ScanTailor". */
QString fileNamePattern();

/** Adds the extension ".ScanTailor" unless \p path ends with it in any letter case. An empty path stays empty. */
QString withProjectExtension(const QString& path);

/** The project file of a new project in \p inputDir: named like the folder and saved into it. */
QString defaultProjectFile(const QString& inputDir);

/** The output folder of a new project in \p inputDir. */
QString defaultOutputDir(const QString& inputDir);

/** Whether the file can be added to a project, judged by its extension. */
bool isImportableImage(const QFileInfo& file);

/** The importable images directly in \p dir, without its subfolders. */
QFileInfoList importableImagesIn(const QString& dir);

/** Whether all image files of the project exist. Each file is checked once. */
bool allImagesExist(const ProjectPages& pages);

/**
 * Whether a project with a missing output folder is still where it was: the parent of the
 * output folder and all images exist. For a project without images, \p inputDir must exist.
 */
bool isProjectInPlace(const ProjectPages& pages, const QString& outDir, const QString& inputDir);

/**
 * Where the backup copy of a project file from an older version goes: "Book_BACKUP.ScanTailor"
 * next to "Book.ScanTailor", numbered if that file already exists.
 */
QString backupFilePath(const QString& projectFile);
}  // namespace project_files

#endif  // SCANTAILOR_CORE_PROJECTFILES_H_
