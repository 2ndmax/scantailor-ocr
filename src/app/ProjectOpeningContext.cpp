// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectOpeningContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <cassert>

#include "DefaultParamsProvider.h"
#include "FixDpiDialog.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"
#include "version.h"

ProjectOpeningContext::ProjectOpeningContext(QWidget* parent, const QString& projectFile)
    : m_projectFile(projectFile), m_parent(parent) {}

ProjectOpeningContext::~ProjectOpeningContext() {
  // Deleting a null pointer is OK.
  delete m_fixDpiDialog;
}

void ProjectOpeningContext::proceed() {
  ProjectReader::FileResult result = ProjectReader::readFile(m_projectFile);
  switch (result.error) {
    case ProjectReader::FileError::NONE:
      break;
    case ProjectReader::FileError::CANNOT_OPEN:
      deleteLater();
      QMessageBox::warning(m_parent, tr("Error"), tr("Unable to open the project file."));
      return;
    case ProjectReader::FileError::DAMAGED:
      deleteLater();
      QMessageBox::warning(m_parent, tr("Error"), tr("The project file is damaged."));
      return;
    case ProjectReader::FileError::INCOMPATIBLE_VERSION:
      deleteLater();
      QMessageBox::warning(m_parent, tr("Error"),
                           tr("The project file is not compatible with the current application version."));
      return;
    case ProjectReader::FileError::UNREADABLE:
      deleteLater();
      QMessageBox::critical(m_parent, tr("Error"), tr("Unable to interpret the project file."));
      return;
  }
  m_reader = std::move(result.reader);

  if (!confirmOlderVersion()) {
    deleteLater();
    return;
  }

  if (m_reader->pages()->validateDpis()) {
    deleteLater();
    emit done(this);
    return;
  }

  showFixDpiDialog();
}

namespace {
/** The name of the active profile of the default parameters, as the Default Parameters dialog shows it. */
QString profileDisplayName() {
  const QString name = DefaultParamsProvider::getInstance().getProfileName();
  if (name == "Default") {
    return QCoreApplication::translate("DefaultParamsDialog", "Default");
  }
  if (name == "Minimal") {
    return QCoreApplication::translate("DefaultParamsDialog", "Minimal");
  }
  if (name == "Custom") {
    return QCoreApplication::translate("DefaultParamsDialog", "Custom");
  }
  return name;
}
}  // namespace

bool ProjectOpeningContext::confirmOlderVersion() {
  const int version = m_reader->getVersion().toInt();
  if ((version == PROJECT_VERSION) || m_projectFile.isEmpty()) {
    return true;
  }

  const QString backupFile = project_files::backupFilePath(m_projectFile);
  QMessageBox box(QMessageBox::Warning, tr("Project from an Older Version"),
                  tr("This project was saved by ScanTailor Advanced or an older version (project file version %2). "
                     "ScanTailor OCR can open it, but processes some settings differently, so the results can "
                     "differ. Settings that the project doesn't contain are taken from the profile \"%1\" of the "
                     "default parameters.")
                          .arg(profileDisplayName(), QString::number(version))
                      + "\n\n"
                      + tr("Once the project is saved, also automatically, ScanTailor Advanced can no longer open "
                           "it. A backup copy keeps the original file usable there. It's saved next to the project "
                           "as \"%1\".")
                            .arg(QFileInfo(backupFile).fileName()),
                  QMessageBox::NoButton, m_parent);
  QPushButton* backupButton = box.addButton(tr("Create backup and open"), QMessageBox::AcceptRole);
  QPushButton* openButton = box.addButton(tr("Open without backup"), QMessageBox::AcceptRole);
  box.addButton(QMessageBox::Cancel);
  box.setDefaultButton(backupButton);
  box.exec();

  if (box.clickedButton() == openButton) {
    return true;
  }
  if (box.clickedButton() != backupButton) {
    return false;
  }
  if (!QFile::copy(m_projectFile, backupFile)) {
    QMessageBox::warning(m_parent, tr("Error"),
                         tr("The backup copy \"%1\" could not be created. The project was not opened.")
                             .arg(QDir::toNativeSeparators(backupFile)));
    return false;
  }
  return true;
}

void ProjectOpeningContext::fixedDpiSubmitted() {
  m_reader->pages()->updateMetadataFrom(m_fixDpiDialog->files());
  emit done(this);
}

void ProjectOpeningContext::fixDpiDialogDestroyed() {
  deleteLater();
}

void ProjectOpeningContext::showFixDpiDialog() {
  assert(!m_fixDpiDialog);
  m_fixDpiDialog = new FixDpiDialog(m_reader->pages()->toImageFileInfo(), m_parent);
  m_fixDpiDialog->setAttribute(Qt::WA_DeleteOnClose);
  m_fixDpiDialog->setAttribute(Qt::WA_QuitOnClose, false);
  if (m_parent) {
    m_fixDpiDialog->setWindowModality(Qt::WindowModal);
  }
  connect(m_fixDpiDialog, &QDialog::accepted, this, &ProjectOpeningContext::fixedDpiSubmitted);
  connect(m_fixDpiDialog, &QObject::destroyed, this, &ProjectOpeningContext::fixDpiDialogDestroyed);
  m_fixDpiDialog->show();
}
