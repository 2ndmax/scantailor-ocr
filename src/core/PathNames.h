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
}  // namespace path_names

#endif  // SCANTAILOR_CORE_PATHNAMES_H_
