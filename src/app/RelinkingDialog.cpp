// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RelinkingDialog.h"

#include <QDir>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMessageBox>
#include <algorithm>

#include "RelinkingSortingModel.h"

namespace {
/**
 * RelinkablePath::normalize() turns a drive root into "E:", which Qt takes for a path relative
 * to the current folder of that drive. This adds the slash back for checks with QDir and QFileInfo.
 */
QString withRootSlash(const QString& path) {
  return path.endsWith(QChar(':')) ? path + QChar('/') : path;
}

bool isExistingDir(const QString& path) {
  return QFileInfo(withRootSlash(path)).isDir();
}

/** Returns the deepest folder of \p path that still exists, or \p fallback if there is none. */
QString existingAncestor(QString path, const QString& fallback) {
  while (!path.isEmpty() && !isExistingDir(path)) {
    path.truncate(std::max(0, static_cast<int>(path.lastIndexOf(QChar('/')))));
  }
  return path.isEmpty() ? fallback : withRootSlash(path);
}
}  // namespace

RelinkingDialog::RelinkingDialog(const QString& projectFilePath, QWidget* parent)
    : QDialog(parent),
      m_sortingModel(new RelinkingSortingModel(this)),
      m_projectFileDir(QFileInfo(projectFilePath).path()),
      m_nodeType(RelinkablePath::Dir),
      m_outputDirShown(false),
      m_initialStateApplied(false),
      m_showMissingOnlyTouched(false) {
  ui.setupUi(this);
  m_sortingModel->setSourceModel(&m_model);
  ui.treeView->setModel(m_sortingModel);
  ui.treeView->header()->setStretchLastSection(false);
  ui.treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  ui.treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  ui.outDirLine->installEventFilter(this);
  ui.nodePathLine->installEventFilter(this);
  refreshNodePath();

  connect(ui.treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
          &RelinkingDialog::selectionChanged);

  connect(ui.nodePathLine, &QLineEdit::editingFinished, this, &RelinkingDialog::nodePathEdited);
  connect(ui.locateBtn, &QAbstractButton::clicked, this, &RelinkingDialog::locateClicked);

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
  connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &RelinkingDialog::okClicked);
}

bool RelinkingDialog::eventFilter(QObject* watched, QEvent* event) {
  if (((watched == ui.outDirLine) || (watched == ui.nodePathLine)) && (event->type() == QEvent::KeyPress)) {
    const int key = static_cast<QKeyEvent*>(event)->key();
    if ((key == Qt::Key_Return) || (key == Qt::Key_Enter)) {
      // Apply the path, but don't let Enter close the dialog.
      if (watched == ui.outDirLine) {
        outputDirEdited();
      } else {
        nodePathEdited();
      }
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void RelinkingDialog::selectionChanged() {
  // The selected node stays visible with "Show missing only" until something else is selected.
  const QModelIndexList rows(ui.treeView->selectionModel()->selectedRows());
  m_sortingModel->setPinned(rows.isEmpty() ? QModelIndex() : m_sortingModel->mapToSource(rows.front()));
  refreshNodePath();
}

void RelinkingDialog::nodePathEdited() {
  applyNodePath(ui.nodePathLine->text().trimmed());
}

void RelinkingDialog::locateClicked() {
  if (m_nodePath.isEmpty()) {
    return;
  }

  const bool isFile = (m_nodeType == RelinkablePath::File);
  const QString startPath(existingAncestor(m_nodePath, m_projectFileDir));
  const QString title(tr("Locate %1").arg(RelinkingModel::toDisplayPath(m_nodePath)));
  // Native dialogs, like the one for the output folder.
  const QString path(isFile ? QFileDialog::getOpenFileName(this, title, startPath)
                            : QFileDialog::getExistingDirectory(this, title, startPath));
  if (!path.isEmpty()) {
    applyNodePath(path);
  }
}

void RelinkingDialog::applyNodePath(const QString& path) {
  if (m_nodePath.isEmpty()) {
    return;
  }

  // The new path replaces the selected folder or file as a whole.
  const QString newPath(RelinkablePath::normalize(path));
  if (!newPath.isEmpty() && QDir::isAbsolutePath(withRootSlash(newPath)) && (newPath != m_nodePath)) {
    RelinkingModel::Merge merge;
    // m_nodePath changes if the selection moves while the model updates, so it's copied.
    const QString oldPath(m_nodePath);
    const auto type = (RelinkablePath::Type) m_nodeType;
    const QFileInfo target(withRootSlash(newPath));
    if (target.exists() && (target.isDir() != (type == RelinkablePath::Dir))) {
      QMessageBox::warning(this, tr("Relinking"),
                           (type == RelinkablePath::Dir)
                               ? tr("This change was not applied, because %1 is a file, not a folder.")
                                     .arg(RelinkingModel::toDisplayPath(newPath))
                               : tr("This change was not applied, because %1 is a folder, not a file.")
                                     .arg(RelinkingModel::toDisplayPath(newPath)));
    } else if (m_model.replacePrefix(oldPath, newPath, type, &merge)) {
      refreshOutputDir();  // The output folder follows a relinked image folder it's in.
    } else {
      QString text(tr("This change was not applied, because several images would then use the same file:\n\n"
                      "%1\nand\n%2\nwould both become\n%3")
                       .arg(RelinkingModel::toDisplayPath(merge.origPath1),
                            RelinkingModel::toDisplayPath(merge.origPath2),
                            RelinkingModel::toDisplayPath(merge.newPath)));
      if (merge.count > 1) {
        text += QLatin1String("\n\n") + tr("%n file(s) would be shared in total.", nullptr, merge.count);
      }
      QMessageBox::warning(this, tr("Relinking"), text);
    }
  }

  refreshNodePath();  // Also restores the field if the path was rejected.
}

void RelinkingDialog::refreshNodePath() {
  const QModelIndexList rows(ui.treeView->selectionModel()->selectedRows());
  if (rows.isEmpty()) {
    m_nodePath.clear();
    ui.nodePathLine->clear();
    ui.nodePathLine->setEnabled(false);
    ui.locateBtn->setEnabled(false);
    ui.nodeStatusLabel->setPixmap(RelinkingModel::stateIcon(RelinkingModel::StatePending));
    return;
  }

  const QModelIndex index(rows.front());
  m_nodePath = index.data(RelinkingModel::PathRole).toString();
  m_nodeType = index.data(RelinkingModel::TypeRole).toInt();
  ui.nodePathLine->setText(RelinkingModel::toDisplayPath(m_nodePath));
  ui.nodePathLine->setEnabled(true);
  ui.locateBtn->setEnabled(true);
  refreshNodeStatus();
}

void RelinkingDialog::refreshNodeStatus() {
  const QModelIndexList rows(ui.treeView->selectionModel()->selectedRows());
  if (!rows.isEmpty()) {
    ui.nodeStatusLabel->setPixmap(rows.front().data(Qt::DecorationRole).value<QPixmap>());
  }
}

void RelinkingDialog::okClicked() {
  if (m_model.hasOutputDir()) {
    // Same questions as when creating a project, so their translations are shared.
    const QString outDir(m_model.outputDir());
    if (!isExistingDir(outDir)) {
      if (QMessageBox::question(
              this, QCoreApplication::translate("ProjectFilesDialog", "Create Folder?"),
              QCoreApplication::translate("ProjectFilesDialog", "Output folder doesn't exist. Create it?"),
              QMessageBox::Yes | QMessageBox::No)
          != QMessageBox::Yes) {
        return;
      }
      if (!QDir().mkpath(withRootSlash(outDir))) {
        QMessageBox::warning(this, QCoreApplication::translate("ProjectFilesDialog", "Error"),
                             QCoreApplication::translate("ProjectFilesDialog", "Unable to create output folder."));
        return;
      }
      refreshOutputDir();
    }
  }

  accept();
}

void RelinkingDialog::outputDirEdited() {
  const QString path(RelinkablePath::normalize(ui.outDirLine->text().trimmed()));
  if (!path.isEmpty() && QDir::isAbsolutePath(withRootSlash(path)) && (path != m_model.outputDir())) {
    m_model.setOutputDir(path);
  }
  refreshOutputDir();  // Also restores the field if the path was rejected.
}

void RelinkingDialog::outputDirBrowseClicked() {
  const QString startPath(existingAncestor(m_model.outputDir(), m_projectFileDir));
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

  refreshNodeStatus();

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

  const bool exists = isExistingDir(path);
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
