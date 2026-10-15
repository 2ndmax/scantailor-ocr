// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RelinkingDialog.h"

#include <core/IconProvider.h>

#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <algorithm>
#include <cassert>

#include "RelinkingSortingModel.h"

RelinkingDialog::RelinkingDialog(const QString& projectFilePath, QWidget* parent)
    : QDialog(parent),
      m_sortingModel(new RelinkingSortingModel(this)),
      m_projectFileDir(QFileInfo(projectFilePath).path()),
      m_outputDirShown(false),
      m_initialStateApplied(false),
      m_showMissingOnlyTouched(false) {
  ui.setupUi(this);
  ui.undoButton->setIcon(IconProvider::getInstance().getIcon("undo"));
  m_sortingModel->setSourceModel(&m_model);
  ui.treeView->setModel(m_sortingModel);
  ui.treeView->header()->setStretchLastSection(false);
  ui.treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui.treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  ui.errorLabel->setVisible(false);
  ui.undoButton->setVisible(false);
  ui.pathVisualization->setVisible(false);
  ui.outDirLine->installEventFilter(this);

  connect(ui.treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &RelinkingDialog::selectionChanged);

  connect(ui.pathVisualization, &RelinkablePathVisualization::clicked, this, &RelinkingDialog::pathButtonClicked);

  connect(ui.undoButton, &QAbstractButton::clicked, this, &RelinkingDialog::undoButtonClicked);

  connect(ui.outDirLine, &QLineEdit::editingFinished, this, &RelinkingDialog::outputDirEdited);
  connect(ui.outDirBrowseBtn, &QAbstractButton::clicked, this, &RelinkingDialog::outputDirBrowseClicked);
  connect(ui.showMissingOnlyCB, &QCheckBox::toggled, this, &RelinkingDialog::showMissingOnlyToggled);

  // Paths are added after construction and their status arrives in the background,
  // so the summary and the initial state are updated in batches.
  m_statusUpdateTimer.setSingleShot(true);
  m_statusUpdateTimer.setInterval(100);
  connect(&m_statusUpdateTimer, &QTimer::timeout, this, &RelinkingDialog::updateStatus);
  connect(&m_model, &QAbstractItemModel::dataChanged, &m_statusUpdateTimer, qOverload<>(&QTimer::start));
  connect(&m_model, &QAbstractItemModel::rowsInserted, &m_statusUpdateTimer, qOverload<>(&QTimer::start));
  QTimer::singleShot(0, this, &RelinkingDialog::updateStatus);

  disconnect(ui.buttonBox, &QDialogButtonBox::accepted, nullptr, nullptr);
  connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &RelinkingDialog::commitChanges);
}

bool RelinkingDialog::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == ui.outDirLine) && (event->type() == QEvent::KeyPress)) {
    const int key = static_cast<QKeyEvent*>(event)->key();
    if ((key == Qt::Key_Return) || (key == Qt::Key_Enter)) {
      // Apply the path, but don't let Enter close the dialog.
      outputDirEdited();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void RelinkingDialog::selectionChanged(const QItemSelection& selected, const QItemSelection& deselected) {
  if (selected.isEmpty()) {
    ui.pathVisualization->clear();
    ui.pathVisualization->setVisible(false);
  } else {
    ui.undoButton->setVisible(false);

    const QModelIndex index(selected.front().topLeft());
    const QString path(index.data(RelinkingModel::UncommittedPathRole).toString());
    const int type = index.data(RelinkingModel::TypeRole).toInt();
    ui.pathVisualization->setPath(RelinkablePath(path, (RelinkablePath::Type) type), /*clickable=*/true);
    ui.pathVisualization->setVisible(true);

    if (ui.errorLabel->isVisible()) {
      m_model.rollbackChanges();
    } else {
      m_model.commitChanges();
    }
  }

  ui.errorLabel->setVisible(false);
  refreshOutputDir();
}

void RelinkingDialog::pathButtonClicked(const QString& prefixPath, const QString& suffixPath, const int type) {
  assert(!prefixPath.endsWith(QChar('/')) && !prefixPath.endsWith(QChar('\\')));
  assert(!suffixPath.startsWith(QChar('/')) && !suffixPath.startsWith(QChar('\\')));

  QString replacementPath;

  if (type == RelinkablePath::File) {
    const QDir dir(QFileInfo(prefixPath).dir());
    replacementPath = QFileDialog::getOpenFileName(
        this, tr("Substitution File for %1").arg(QDir::toNativeSeparators(prefixPath)),
        dir.exists() ? dir.path() : m_projectFileDir, QString(), nullptr, QFileDialog::DontUseNativeDialog);
  } else {
    const QDir dir(prefixPath);
    replacementPath = QFileDialog::getExistingDirectory(
        this, tr("Substitution Folder for %1").arg(QDir::toNativeSeparators(prefixPath)),
        dir.exists() ? prefixPath : m_projectFileDir, QFileDialog::DontUseNativeDialog);
  }
  // So what's wrong with native dialogs? The one for directory selection won't show files
  // at all (if you ask it to, the non-native dialog will appear), which is inconvenient
  // in this situation. So, if one of them has to be non-native, the other was made
  // non-native as well, for consistency reasons.
  replacementPath = RelinkablePath::normalize(replacementPath);

  if (replacementPath.isEmpty()) {
    return;
  }

  if (prefixPath == replacementPath) {
    return;
  }

  QString newPath(replacementPath);
  newPath += QChar('/');
  newPath += suffixPath;

  m_model.replacePrefix(prefixPath, replacementPath, (RelinkablePath::Type) type);

  if (m_model.checkForMerges()) {
    ui.errorLabel->setText(tr("This change would merge several files into one."));
    ui.errorLabel->setVisible(true);
    ui.pathVisualization->clear();
    ui.pathVisualization->setVisible(false);
  } else {
    ui.pathVisualization->setPath(RelinkablePath(newPath, (RelinkablePath::Type) type), /*clickable=*/false);
    ui.pathVisualization->setVisible(true);
  }

  ui.undoButton->setVisible(true);
  ui.treeView->viewport()->update();
  refreshOutputDir();  // The output folder follows a relinked image folder it's in.
}  // RelinkingDialog::pathButtonClicked

void RelinkingDialog::undoButtonClicked() {
  m_model.rollbackChanges();  // Has to go before selectionChanged()
  selectionChanged(ui.treeView->selectionModel()->selection(), QItemSelection());
}

void RelinkingDialog::commitChanges() {
  if (m_model.hasOutputDir()) {
    // Same questions as when creating a project, so their translations are shared.
    const QString outDir(m_model.outputDir());
    if (!QFileInfo(outDir).isDir()) {
      if (QMessageBox::question(
              this, QCoreApplication::translate("ProjectFilesDialog", "Create Folder?"),
              QCoreApplication::translate("ProjectFilesDialog", "Output folder doesn't exist.  Create it?"),
              QMessageBox::Yes | QMessageBox::No)
          != QMessageBox::Yes) {
        return;
      }
      if (!QDir().mkpath(outDir)) {
        QMessageBox::warning(this, QCoreApplication::translate("ProjectFilesDialog", "Error"),
                             QCoreApplication::translate("ProjectFilesDialog", "Unable to create output folder."));
        return;
      }
      refreshOutputDir();
    }
  }

  m_model.commitChanges();
  accept();
}

void RelinkingDialog::outputDirEdited() {
  const QString path(RelinkablePath::normalize(ui.outDirLine->text().trimmed()));
  if (!path.isEmpty() && QDir::isAbsolutePath(path) && (path != m_model.outputDir())) {
    m_model.setOutputDir(path);
  }
  refreshOutputDir();  // Also restores the field if the path was rejected.
}

void RelinkingDialog::outputDirBrowseClicked() {
  // Start at the deepest folder of the current path that still exists.
  QString startPath(m_model.outputDir());
  while (!startPath.isEmpty() && !QFileInfo(startPath).isDir()) {
    startPath.truncate(std::max(0, static_cast<int>(startPath.lastIndexOf(QChar('/')))));
  }
  if (startPath.isEmpty()) {
    startPath = m_projectFileDir;
  }

  const QString path(
      RelinkablePath::normalize(QFileDialog::getExistingDirectory(this, tr("Output Folder"), startPath)));
  if (!path.isEmpty() && (path != m_model.outputDir())) {
    m_model.setOutputDir(path);
  }
  refreshOutputDir();
}

void RelinkingDialog::showMissingOnlyToggled(const bool checked) {
  if (!m_initialStateApplied) {
    m_showMissingOnlyTouched = true;
  }
  m_sortingModel->setShowMissingOnly(checked);
}

void RelinkingDialog::updateStatus() {
  if (!m_outputDirShown) {
    // The paths have been collected by now.
    m_outputDirShown = true;
    ui.outputGroup->setVisible(m_model.hasOutputDir());
    refreshOutputDir();
  }

  if (m_model.isStatusUpdatePending()) {
    ui.imagesSummaryLabel->setText(tr("Checking files ..."));
    return;
  }

  ui.imagesSummaryLabel->setText(tr("%1 of %2 missing").arg(m_model.missingFileCount()).arg(m_model.fileCount()));

  if (!m_initialStateApplied) {
    applyInitialState();
  }
}

void RelinkingDialog::refreshOutputDir() {
  if (!m_model.hasOutputDir()) {
    return;
  }

  const QString path(m_model.outputDir());
  ui.outDirLine->setText(RelinkingModel::toDisplayPath(path));

  const bool exists = QFileInfo(path).isDir();
  ui.outDirStatusLabel->setPixmap(
      RelinkingModel::stateIcon(exists ? RelinkingModel::AllPresent : RelinkingModel::FolderMissing));
  ui.outDirStatusLabel->setToolTip(exists ? tr("The folder exists.") : tr("The folder is missing."));
}

void RelinkingDialog::applyInitialState() {
  m_initialStateApplied = true;

  if (!m_showMissingOnlyTouched) {
    ui.showMissingOnlyCB->setChecked(m_model.missingFileCount() > 0);
  }

  // Expand only the folders that exist but miss some of their files.
  // A missing folder is to be relinked as a whole, so its files are of no interest.
  const int rows = m_sortingModel->rowCount();
  for (int row = 0; row < rows; ++row) {
    const QModelIndex proxyIndex(m_sortingModel->index(row, 0));
    ui.treeView->setExpanded(proxyIndex, m_model.isPartiallyMissingFolder(m_sortingModel->mapToSource(proxyIndex)));
  }
}
