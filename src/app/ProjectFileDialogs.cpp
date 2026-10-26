// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectFileDialogs.h"

#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>

#include "ProjectFiles.h"
#include "RecentProjects.h"

namespace project_file_dialogs {
QString askForFileToSave(QWidget* parent, const QString& currentProjectFile) {
  QString projectDir;
  if (!currentProjectFile.isEmpty()) {
    projectDir = QFileInfo(currentProjectFile).absolutePath();
  } else {
    QSettings settings;
    projectDir = settings.value("project/lastDir").toString();
  }

  // The translation of the main window is used.
  const QString projectFile(
      QFileDialog::getSaveFileName(parent, QString(), projectDir,
                                   QCoreApplication::translate("MainWindow", "ScanTailor OCR Projects") + " ("
                                       + project_files::fileNamePattern() + ')'));
  return project_files::withProjectExtension(projectFile);
}

void rememberSavedProject(const QString& projectFile) {
  QSettings settings;
  settings.setValue("project/lastDir", QFileInfo(projectFile).absolutePath());

  RecentProjects rp;
  rp.read();
  rp.setMostRecent(projectFile);
  rp.write();
}
}  // namespace project_file_dialogs
