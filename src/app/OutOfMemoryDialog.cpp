// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutOfMemoryDialog.h"

#include <QMessageBox>
#include <utility>

#include "ProjectFileDialogs.h"
#include "ProjectWriter.h"

OutOfMemoryDialog::OutOfMemoryDialog(QWidget* parent) : QDialog(parent) {
  ui.setupUi(this);

  ui.topLevelStack->setCurrentWidget(ui.mainPage);

  connect(ui.saveProjectBtn, &QAbstractButton::clicked, this, &OutOfMemoryDialog::saveProject);
  connect(ui.saveProjectAsBtn, &QAbstractButton::clicked, this, &OutOfMemoryDialog::saveProjectAs);
  connect(ui.dontSaveBtn, &QAbstractButton::clicked, this, &OutOfMemoryDialog::reject);
}

void OutOfMemoryDialog::setParams(const QString& projectFile,
                                  std::shared_ptr<StageSequence> stages,
                                  std::shared_ptr<ProjectPages> pages,
                                  const SelectedPage& selectedPage,
                                  const OutputFileNameGenerator& outFileNameGen,
                                  const QString& emptyProjectInputDir) {
  m_projectFile = projectFile;
  m_stages = std::move(stages);
  m_pages = std::move(pages);
  m_selectedPage = selectedPage;
  m_outFileNameGen = outFileNameGen;
  m_emptyProjectInputDir = emptyProjectInputDir;

  ui.saveProjectBtn->setVisible(!projectFile.isEmpty());
}

void OutOfMemoryDialog::saveProject() {
  if (m_projectFile.isEmpty()) {
    saveProjectAs();
  } else if (saveProjectWithFeedback(m_projectFile)) {
    showSaveSuccessScreen();
  }
}

void OutOfMemoryDialog::saveProjectAs() {
  const QString projectFile = project_file_dialogs::askForFileToSave(this, m_projectFile);
  if (projectFile.isEmpty()) {
    return;
  }

  if (saveProjectWithFeedback(projectFile)) {
    m_projectFile = projectFile;
    showSaveSuccessScreen();
    project_file_dialogs::rememberSavedProject(m_projectFile);
  }
}

bool OutOfMemoryDialog::saveProjectWithFeedback(const QString& projectFile) {
  ProjectWriter writer(m_pages, m_selectedPage, m_outFileNameGen, m_emptyProjectInputDir);

  if (!writer.write(projectFile, m_stages->filters(), *m_stages->processingOptions(), *m_stages->defaultParams())) {
    QMessageBox::warning(this, tr("Error"), tr("Error saving the project file!"));
    return false;
  }
  return true;
}

void OutOfMemoryDialog::showSaveSuccessScreen() {
  ui.topLevelStack->setCurrentWidget(ui.saveSuccessPage);
}
