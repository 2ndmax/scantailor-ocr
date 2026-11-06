// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectOpeningContext.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <cassert>

#include "DefaultParamsProvider.h"
#include "FixDpiDialog.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"

ProjectOpeningContext::ProjectOpeningContext(QWidget* parent, const QString& projectFile)
    : m_projectFile(projectFile), m_parent(parent) {}

ProjectOpeningContext::~ProjectOpeningContext() {
  // Deleting a null pointer is OK.
  delete m_fixDpiDialog;
}

void ProjectOpeningContext::proceed() {
  ProjectReader::FileResult result = ProjectReader::readFile(m_projectFile);
  if (result.error != ProjectReader::FileError::NONE) {
    deleteLater();
    const QString message = ProjectReader::errorMessage(result.error);
    if (result.error == ProjectReader::FileError::UNREADABLE) {
      QMessageBox::critical(m_parent, tr("Error"), message);
    } else {
      QMessageBox::warning(m_parent, tr("Error"), message);
    }
    return;
  }
  m_reader = std::move(result.reader);

  if (!confirmOpening()) {
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

bool ProjectOpeningContext::confirmOpening() {
  const bool olderVersion = m_reader->isFromOlderVersion();
  const int numDamagedImages = m_reader->numDamagedImages();
  if ((!olderVersion && (numDamagedImages == 0)) || m_projectFile.isEmpty()) {
    return true;
  }

  // REVIEW-NOTE (2026-11): no regression test for the message on damaged images -> qt_tests doesn't build the
  // dialogs of the app. Added after a hand test; the command line tool's refusal is tested.
  // Both cases may apply to one file; one message then offers the one backup copy.
  const QString backupFile = project_files::backupFilePath(m_projectFile);
  const QString backupName = QFileInfo(backupFile).fileName();
  QStringList paragraphs;
  if (olderVersion) {
    paragraphs << tr("This project was saved by ScanTailor Advanced or an older version (project file version %2). "
                     "ScanTailor OCR can open it, but processes some settings differently, so the results can "
                     "differ. Settings that the project doesn't contain are taken from the profile \"%1\" of the "
                     "default parameters.")
                      .arg(profileDisplayName(), QString::number(m_reader->getVersion().toInt()));
  }
  if (numDamagedImages > 0) {
    paragraphs << tr(
        "The project file is damaged, for example because it was edited by hand. The project opens "
        "without the %n image(s) that can't be read.",
        nullptr, numDamagedImages);
  }
  if (olderVersion) {
    paragraphs << tr("Once the project is saved, also automatically, ScanTailor Advanced can no longer open "
                     "it. A backup copy keeps the original file usable there. It's saved next to the project "
                     "as \"%1\".")
                      .arg(backupName);
  } else {
    paragraphs << tr("Once the project is saved, also automatically, the original file is replaced. A backup copy "
                     "keeps it. It's saved next to the project as \"%1\".")
                      .arg(backupName);
  }
  QMessageBox box(QMessageBox::Warning, olderVersion ? tr("Project from an Older Version") : tr("Damaged Project File"),
                  paragraphs.join("\n\n"), QMessageBox::NoButton, m_parent);
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
