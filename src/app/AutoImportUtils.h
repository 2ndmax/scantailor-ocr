// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_AUTOIMPORTUTILS_H_
#define SCANTAILOR_APP_AUTOIMPORTUTILS_H_

#include <QSet>
#include <QString>
#include <memory>

class ProjectPages;

namespace auto_import_utils {
/**
 * \brief A file path key for matching images in the project.
 *
 * Cleaned and absolute, and case-folded on Windows so paths that differ only in case match.
 */
QString imagePathKey(const QString& filePath);

QSet<QString> projectImagePathKeys(const ProjectPages& pages);

int countImagesWithoutDpi(const ProjectPages& pages);

QString suggestedImportDirectory(const ProjectPages& pages, const QString& emptyProjectInputDir);

bool renameRetrying(const QString& from, const QString& to);
}  // namespace auto_import_utils

#endif  // ifndef SCANTAILOR_APP_AUTOIMPORTUTILS_H_
