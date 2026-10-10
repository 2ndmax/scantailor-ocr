// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PROJECTFILEDIALOGS_H_
#define SCANTAILOR_APP_PROJECTFILEDIALOGS_H_

#include <QString>

class QWidget;

// Shared by the main window and the out of memory dialog.
namespace project_file_dialogs {
/**
 * Asks where to save the project, starting in the folder of \p currentProjectFile
 * or else in the last used project folder.  Adds ".ScanTailor" if missing.
 *
 * \return The chosen file, or an empty string if the user cancelled.
 */
QString askForFileToSave(QWidget* parent, const QString& currentProjectFile);

/**
 * Remembers the folder of a saved project and makes it the most recent project.
 */
void rememberSavedProject(const QString& projectFile);
}  // namespace project_file_dialogs

#endif  // SCANTAILOR_APP_PROJECTFILEDIALOGS_H_
