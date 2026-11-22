// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PATHNAMES_H_
#define SCANTAILOR_CORE_PATHNAMES_H_

#include <QString>

/**
 * Rules for the names of new files and folders. They are the rules of Windows and apply on all
 * systems, because projects and exported files move between computers.
 */
namespace path_names {
/** Why \p name can't be the name of a new file or folder, as a translated message. Empty if it can. */
QString invalidNameReason(const QString& name);

/**
 * Why \p path can't be created, as a translated message. Empty if it can. Only the parts that
 * don't exist yet are judged, so a path may lead through any existing folder.
 */
QString invalidNewPathReason(const QString& path);

/**
 * Whether files can be created in the existing folder \p dirPath. QFileInfo::isWritable() is not
 * reliable on Windows, so a temporary file is created and removed instead.
 */
bool isWritableDir(const QString& dirPath);

/**
 * Whether \p dirPath is a folder files can be created in, or a folder that can be created in such
 * a place. For a folder that doesn't exist yet, the nearest existing ancestor is probed; nothing
 * is created. Returns false if the path or an existing ancestor is a file.
 */
bool canCreateOrWriteDir(const QString& dirPath);

/**
 * Whether a file can be written at \p filePath, as QSaveFile does it: its folder exists and files
 * can be created in it, and a file already there is not read-only. Nothing is created or changed.
 */
bool canWriteFile(const QString& filePath);
}  // namespace path_names

#endif  // SCANTAILOR_CORE_PATHNAMES_H_
