// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectFilesDialog.h"

#include <core/IconProvider.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QVector>
#include <algorithm>
#include <deque>

#include "ImageLoadErrors.h"
#include "ImageMetadataLoader.h"
#include "NonCopyable.h"
#include "ScanFolderWatcher.h"
#include "SmartFilenameOrdering.h"

class ProjectFilesDialog::Item {
 public:
  enum Status { STATUS_DEFAULT, STATUS_LOAD_OK, STATUS_LOAD_FAILED };

  Item(const QFileInfo& fileInfo, Qt::ItemFlags flags)
      : m_fileInfo(fileInfo), m_flags(flags), m_status(STATUS_DEFAULT) {}

  const QFileInfo& fileInfo() const { return m_fileInfo; }

  Qt::ItemFlags flags() const { return m_flags; }

  Status status() const { return m_status; }

  void setStatus(Status status) { m_status = status; }

  const QString& loadError() const { return m_loadError; }

  void setLoadError(const QString& error) { m_loadError = error; }

  const std::vector<ImageMetadata>& perPageMetadata() const { return m_perPageMetadata; }

  std::vector<ImageMetadata>& perPageMetadata() { return m_perPageMetadata; }

 private:
  QFileInfo m_fileInfo;
  Qt::ItemFlags m_flags;
  std::vector<ImageMetadata> m_perPageMetadata;
  Status m_status;
  QString m_loadError;
};


class ProjectFilesDialog::FileList : private QAbstractListModel {
  DECLARE_NON_COPYABLE(FileList)

 public:
  enum LoadStatus { LOAD_OK, LOAD_FAILED, NO_MORE_FILES };

  FileList();

  ~FileList() override;

  QAbstractItemModel* model() { return this; }

  template <typename OutFunc>
  void files(OutFunc out) const;

  const Item& item(const QModelIndex& index) { return m_items[index.row()]; }

  template <typename OutFunc>
  void items(OutFunc out) const;

  template <typename OutFunc>
  void items(const QItemSelection& selection, OutFunc out) const;

  size_t count() const { return m_items.size(); }

  void clear();

  template <typename It>
  void append(It begin, It end);

  template <typename It>
  void assign(It begin, It end);

  void remove(const QItemSelection& selection);

  void prepareForLoadingFiles();

  LoadStatus loadNextFile();

 private:
  int rowCount(const QModelIndex& parent) const override;

  QVariant data(const QModelIndex& index, int role) const override;

  Qt::ItemFlags flags(const QModelIndex& index) const override;

  std::vector<Item> m_items;
  std::deque<int> m_itemsToLoad;
};


class ProjectFilesDialog::SortedFileList : private QSortFilterProxyModel {
  DECLARE_NON_COPYABLE(SortedFileList)

 public:
  explicit SortedFileList(FileList& delegate);

  QAbstractProxyModel* model() { return this; }

 private:
  bool lessThan(const QModelIndex& lhs, const QModelIndex& rhs) const override;

  FileList& m_delegate;
};


class ProjectFilesDialog::ItemVisualOrdering {
 public:
  bool operator()(const Item& lhs, const Item& rhs) const;
};


template <typename OutFunc>
void ProjectFilesDialog::FileList::files(OutFunc out) const {
  auto it(m_items.begin());
  const auto end(m_items.end());
  for (; it != end; ++it) {
    out(it->fileInfo());
  }
}

template <typename OutFunc>
void ProjectFilesDialog::FileList::items(OutFunc out) const {
  std::for_each(m_items.begin(), m_items.end(), out);
}

template <typename OutFunc>
void ProjectFilesDialog::FileList::items(const QItemSelection& selection, OutFunc out) const {
  QListIterator<QItemSelectionRange> it(selection);
  while (it.hasNext()) {
    const QItemSelectionRange& range = it.next();
    for (int row = range.top(); row <= range.bottom(); ++row) {
      out(m_items[row]);
    }
  }
}

template <typename It>
void ProjectFilesDialog::FileList::append(It begin, It end) {
  if (begin == end) {
    return;
  }
  const size_t count = std::distance(begin, end);
  beginInsertRows(QModelIndex(), static_cast<int>(m_items.size()), static_cast<int>(m_items.size() + count - 1));
  m_items.insert(m_items.end(), begin, end);
  endInsertRows();
}

template <typename It>
void ProjectFilesDialog::FileList::assign(It begin, It end) {
  clear();
  append(begin, end);
}

ProjectFilesDialog::ProjectFilesDialog(QWidget* parent)
    : QDialog(parent),
      m_offProjectFiles(std::make_unique<FileList>()),
      m_offProjectFilesSorted(std::make_unique<SortedFileList>(*m_offProjectFiles)),
      m_inProjectFiles(std::make_unique<FileList>()),
      m_inProjectFilesSorted(std::make_unique<SortedFileList>(*m_inProjectFiles)),
      m_loadTimerId(0),
      m_metadataLoadFailed(false),
      m_autoOutDir(true),
      m_autoProjectFile(true),
      m_existingImagesMode(false) {
  setupUi(this);
  introLabel->hide();

  setupIcons();

  offProjectList->setModel(m_offProjectFilesSorted->model());
  inProjectList->setModel(m_inProjectFilesSorted->model());

  connect(inpDirBrowseBtn, &QAbstractButton::clicked, this, &ProjectFilesDialog::inpDirBrowse);
  connect(outDirBrowseBtn, &QAbstractButton::clicked, this, &ProjectFilesDialog::outDirBrowse);
  connect(inpDirLine, &QLineEdit::textEdited, this, &ProjectFilesDialog::inpDirEdited);
  connect(outDirLine, &QLineEdit::textEdited, this, &ProjectFilesDialog::outDirEdited);
  connect(projectFileBrowseBtn, &QAbstractButton::clicked, this, &ProjectFilesDialog::projectFileBrowse);
  connect(projectFileLine, &QLineEdit::textEdited, this, &ProjectFilesDialog::projectFileEdited);
  connect(addToProjectBtn, &QAbstractButton::clicked, this, &ProjectFilesDialog::addToProject);
  connect(removeFromProjectBtn, &QAbstractButton::clicked, this, &ProjectFilesDialog::removeFromProject);
  connect(buttonBox, &QDialogButtonBox::accepted, this, &ProjectFilesDialog::onOK);
}

ProjectFilesDialog::~ProjectFilesDialog() = default;

void ProjectFilesDialog::chooseExistingImages(const QString& dir, const std::vector<QFileInfo>& images) {
  m_existingImagesMode = true;
  setWindowTitle(tr("Images Already in the Folder"));
  introLabel->setText(
      tr("These images are already in the folder %1, but not in the project.  Move those that are to be added to "
         "the project to the right.")
          .arg(QDir::toNativeSeparators(dir)));
  introLabel->show();
  for (QWidget* widget : std::initializer_list<QWidget*>{groupBox_2, groupBox, projectFileGroup, importNewScansCB,
                                                         rtlLayoutCB, forceFixDpi}) {
    widget->hide();
  }
  groupBox_4->setTitle(tr("Files to Add"));
  // Used by removeFromProject() to decide which files may go back to the left.
  inpDirLine->setText(QDir::toNativeSeparators(dir));

  // None is chosen at first, so images are only added on purpose.
  std::vector<Item> items;
  for (const QFileInfo& image : images) {
    items.emplace_back(image, Qt::ItemIsSelectable | Qt::ItemIsEnabled);
  }
  m_inProjectFiles->clear();
  m_offProjectFiles->assign(items.begin(), items.end());
}

QString ProjectFilesDialog::inputDirectory() const {
  return inpDirLine->text();
}

QString ProjectFilesDialog::outputDirectory() const {
  return outDirLine->text();
}

QString ProjectFilesDialog::projectFile() const {
  return QDir::fromNativeSeparators(sanitizePath(projectFileLine->text()));
}

bool ProjectFilesDialog::isImportingNewScans() const {
  return importNewScansCB->isChecked();
}


std::vector<ImageFileInfo> ProjectFilesDialog::inProjectFiles() const {
  std::vector<ImageFileInfo> files;
  m_inProjectFiles->items([&](const Item& item) { files.emplace_back(item.fileInfo(), item.perPageMetadata()); });

  std::sort(files.begin(), files.end(), [](const ImageFileInfo& lhs, const ImageFileInfo& rhs) {
    return SmartFilenameOrdering()(lhs.fileInfo(), rhs.fileInfo());
  });
  return files;
}

bool ProjectFilesDialog::isRtlLayout() const {
  return rtlLayoutCB->isChecked();
}

bool ProjectFilesDialog::isDpiFixingForced() const {
  return forceFixDpi->isChecked();
}

QString ProjectFilesDialog::sanitizePath(const QString& path) {
  QString trimmed(path.trimmed());
  if (trimmed.startsWith(QChar('"')) && trimmed.endsWith(QChar('"'))) {
    trimmed.chop(1);
    if (!trimmed.isEmpty()) {
      trimmed.remove(0, 1);
    }
  }
  return trimmed;
}

void ProjectFilesDialog::inpDirBrowse() {
  QSettings settings;

  QString initialDir(inpDirLine->text());
  if (initialDir.isEmpty() || !QDir(initialDir).exists()) {
    initialDir = settings.value("lastInputDir").toString();
  }
  if (initialDir.isEmpty() || !QDir(initialDir).exists()) {
    initialDir = QDir::home().absolutePath();
  } else {
    initialDir = QDir(initialDir).absolutePath();
  }

  const QString dir(QFileDialog::getExistingDirectory(this, tr("Input Folder"), initialDir));

  if (!dir.isEmpty()) {
    setInputDir(dir);
    settings.setValue("lastInputDir", dir);
  }
}

void ProjectFilesDialog::outDirBrowse() {
  QString initialDir(outDirLine->text());
  if (initialDir.isEmpty() || !QDir(initialDir).exists()) {
    initialDir = QDir::home().absolutePath();
  }

  const QString dir(QFileDialog::getExistingDirectory(this, tr("Output Folder"), initialDir));

  if (!dir.isEmpty()) {
    setOutputDir(dir);
  }
}

void ProjectFilesDialog::inpDirEdited(const QString& text) {
  setInputDir(sanitizePath(text), /* autoAddFiles= */ false);
}

void ProjectFilesDialog::outDirEdited(const QString& text) {
  m_autoOutDir = false;
}

void ProjectFilesDialog::projectFileBrowse() {
  QString initialPath(projectFile());
  if (initialPath.isEmpty() || !QFileInfo(initialPath).absoluteDir().exists()) {
    initialPath = QDir::home().absolutePath();
  }

  // Overwriting is asked about when the dialog is accepted.
  QString file(QFileDialog::getSaveFileName(this, tr("Project File"), initialPath,
                                            tr("ScanTailor OCR Projects") + " (*.ScanTailor)", nullptr,
                                            QFileDialog::DontConfirmOverwrite));
  if (file.isEmpty()) {
    return;
  }
  if (!file.endsWith(".ScanTailor", Qt::CaseInsensitive)) {
    file += ".ScanTailor";
  }
  m_autoProjectFile = false;
  setProjectFile(file);
}

void ProjectFilesDialog::projectFileEdited(const QString& text) {
  m_autoProjectFile = false;
}

namespace {
struct FileInfoLess {
  bool operator()(const QFileInfo& lhs, const QFileInfo& rhs) const {
    if (lhs == rhs) {
      // This takes into account filesystem's case sensitivity.
      return false;
    }
    return lhs.absoluteFilePath() < rhs.absoluteFilePath();
  }
};

/** The topmost selected row of the list, or -1 if nothing is selected. */
int firstSelectedRow(const QListView* list) {
  int row = -1;
  for (const QModelIndex& index : list->selectionModel()->selectedIndexes()) {
    if ((row < 0) || (index.row() < row)) {
      row = index.row();
    }
  }
  return row;
}

/**
 * Selects the entry that took the place of the moved ones, or the last entry if they were at
 * the end, so the move button can be clicked again right away.
 */
void selectRowAfterMove(QListView* list, const int row) {
  const QAbstractItemModel* model = list->model();
  if ((row < 0) || (model->rowCount() == 0)) {
    return;
  }
  const QModelIndex index = model->index(std::min(row, model->rowCount() - 1), 0);
  list->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
}

}  // namespace

void ProjectFilesDialog::setInputDir(const QString& dir, const bool autoAddFiles) {
  inpDirLine->setText(QDir::toNativeSeparators(dir));
  if (m_autoOutDir) {
    setOutputDir(QDir::cleanPath(QDir(dir).filePath("out")));
  }
  if (m_autoProjectFile) {
    // Named like the input directory and saved into it.
    QString name(QDir(dir).dirName());
    if (name.isEmpty()) {
      name = QStringLiteral("project");
    }
    setProjectFile(QDir::cleanPath(QDir(dir).filePath(name + ".ScanTailor")));
  }

  QFileInfoList files(QDir(dir).entryInfoList(QDir::Files));

  {
    // Filter out files already in project.
    // Here we use simple ordering, which is OK.

    std::vector<QFileInfo> newFiles(files.begin(), files.end());
    std::vector<QFileInfo> existingFiles;
    m_inProjectFiles->files([&](const QFileInfo& fileInfo) { existingFiles.push_back(fileInfo); });
    std::sort(newFiles.begin(), newFiles.end(), FileInfoLess());
    std::sort(existingFiles.begin(), existingFiles.end(), FileInfoLess());

    files.clear();
    std::set_difference(newFiles.begin(), newFiles.end(), existingFiles.begin(), existingFiles.end(),
                        std::back_inserter(files), FileInfoLess());
  }

  using ItemList = std::vector<Item>;
  ItemList items;
  for (const QFileInfo& file : files) {
    Qt::ItemFlags flags;
    if (ScanFolderWatcher::isImportableImage(file)) {
      flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;
    }
    items.emplace_back(file, flags);
  }

  m_offProjectFiles->assign(items.begin(), items.end());

  if (autoAddFiles && (m_inProjectFiles->count() == 0)) {
    offProjectList->selectAll();
    addToProject();
  }
}  // ProjectFilesDialog::setInputDir

void ProjectFilesDialog::setOutputDir(const QString& dir) {
  outDirLine->setText(QDir::toNativeSeparators(dir));
}

void ProjectFilesDialog::setProjectFile(const QString& file) {
  projectFileLine->setText(QDir::toNativeSeparators(file));
}

bool ProjectFilesDialog::checkProjectFile() {
  QString file(projectFile());
  if (!file.isEmpty() && !file.endsWith(".ScanTailor", Qt::CaseInsensitive)) {
    file += ".ScanTailor";
    setProjectFile(file);
  }

  const QFileInfo fileInfo(file);
  if (file.isEmpty() || !fileInfo.isAbsolute() || !fileInfo.absoluteDir().exists() || fileInfo.isDir()) {
    QMessageBox::warning(this, tr("Error"), tr("Project file is not set or its folder doesn't exist."));
    return false;
  }

  if (fileInfo.exists()) {
    return QMessageBox::question(this, tr("Overwrite File?"),
                                 tr("The project file %1 already exists.  Overwrite it?")
                                     .arg(QDir::toNativeSeparators(fileInfo.fileName())),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
           == QMessageBox::Yes;
  }
  return true;
}

void ProjectFilesDialog::addToProject() {
  const int firstRow = firstSelectedRow(offProjectList);
  const QItemSelection selection(
      m_offProjectFilesSorted->model()->mapSelectionToSource(offProjectList->selectionModel()->selection()));

  using ItemList = std::vector<Item>;
  ItemList items;

  m_offProjectFiles->items(selection, [&](const Item& item) { items.push_back(item); });

  m_inProjectFiles->append(items.begin(), items.end());
  m_offProjectFiles->remove(selection);
  selectRowAfterMove(offProjectList, firstRow);
}


void ProjectFilesDialog::removeFromProject() {
  const QDir inputDir(inpDirLine->text());

  const int firstRow = firstSelectedRow(inProjectList);
  const QItemSelection selection(
      m_inProjectFilesSorted->model()->mapSelectionToSource(inProjectList->selectionModel()->selection()));

  using ItemList = std::vector<Item>;
  ItemList items;

  m_inProjectFiles->items(selection, [&](const Item& item) {
    if (item.fileInfo().dir() == inputDir) {
      items.push_back(item);
    }
  });

  m_offProjectFiles->append(items.begin(), items.end());
  m_inProjectFiles->remove(selection);
  selectRowAfterMove(inProjectList, firstRow);
}

void ProjectFilesDialog::onOK() {
  if (m_existingImagesMode) {
    // Choosing none of them is fine, too.
    if (m_inProjectFiles->count() == 0) {
      accept();
    } else {
      startLoadingMetadata();
    }
    return;
  }

  // When importing new scans, the project may start empty.
  if ((m_inProjectFiles->count() == 0) && !isImportingNewScans()) {
    QMessageBox::warning(this, tr("Error"), tr("No files in project!"));
    return;
  }

  const QDir inpDir(inpDirLine->text());
  if (!inpDir.isAbsolute() || !inpDir.exists()) {
    QMessageBox::warning(this, tr("Error"), tr("Input folder is not set or doesn't exist."));
    return;
  }

  const QDir outDir(outDirLine->text());
  if (inpDir == outDir) {
    QMessageBox::warning(this, tr("Error"), tr("Input and output folders can't be the same."));
    return;
  }

  if (!checkProjectFile()) {
    return;
  }

  if (outDir.isAbsolute() && !outDir.exists()) {
    // Maybe create it.
    bool create = m_autoOutDir;
    if (!m_autoOutDir) {
      create = QMessageBox::question(this, tr("Create Folder?"), tr("Output folder doesn't exist.  Create it?"),
                                     QMessageBox::Yes | QMessageBox::No)
               == QMessageBox::Yes;
      if (!create) {
        return;
      }
    }
    if (create) {
      if (!outDir.mkpath(outDir.path())) {
        QMessageBox::warning(this, tr("Error"), tr("Unable to create output folder."));
        return;
      }
    }
  }
  if (!outDir.isAbsolute() || !outDir.exists()) {
    QMessageBox::warning(this, tr("Error"), tr("Output folder is not set or doesn't exist."));
    return;
  }

  if (m_inProjectFiles->count() == 0) {
    accept();
    return;
  }
  startLoadingMetadata();
}  // ProjectFilesDialog::onOK

void ProjectFilesDialog::setInputsEnabled(const bool enabled) {
  for (QWidget* widget : std::initializer_list<QWidget*>{
           inpDirLine, inpDirBrowseBtn, outDirLine, outDirBrowseBtn, projectFileLine, projectFileBrowseBtn,
           importNewScansCB, addToProjectBtn, removeFromProjectBtn, offProjectSelectAllBtn, inProjectSelectAllBtn,
           rtlLayoutCB, forceFixDpi, buttonBox->button(QDialogButtonBox::Ok)}) {
    widget->setEnabled(enabled);
  }
}

void ProjectFilesDialog::startLoadingMetadata() {
  m_inProjectFiles->prepareForLoadingFiles();

  progressBar->setMaximum(static_cast<int>(m_inProjectFiles->count()));
  setInputsEnabled(false);
  offProjectList->clearSelection();
  inProjectList->clearSelection();
  m_loadTimerId = startTimer(0);
  m_metadataLoadFailed = false;
}

void ProjectFilesDialog::timerEvent(QTimerEvent* event) {
  if (event->timerId() != m_loadTimerId) {
    QWidget::timerEvent(event);
    return;
  }

  switch (m_inProjectFiles->loadNextFile()) {
    case FileList::NO_MORE_FILES:
      finishLoadingMetadata();
      break;
    case FileList::LOAD_FAILED:
      m_metadataLoadFailed = true;
      // Fall through.
    case FileList::LOAD_OK:
      progressBar->setValue(progressBar->value() + 1);
      break;
  }
}

void ProjectFilesDialog::finishLoadingMetadata() {
  killTimer(m_loadTimerId);

  setInputsEnabled(true);

  if (m_metadataLoadFailed) {
    progressBar->setValue(0);

    // Name the first few failed files together with the reason.
    const int maxFilesNamed = 3;
    QStringList failures;
    int numFailed = 0;
    m_inProjectFiles->items([&](const Item& item) {
      if (item.status() != Item::STATUS_LOAD_FAILED) {
        return;
      }
      if (++numFailed <= maxFilesNamed) {
        QString line = item.fileInfo().fileName();
        if (!item.loadError().isEmpty()) {
          line += QLatin1String(":\n    ") + item.loadError();
        }
        failures.push_back(line);
      }
    });
    if (numFailed > maxFilesNamed) {
      failures.push_back(tr("... and %n more.", "", numFailed - maxFilesNamed));
    }

    QMessageBox::warning(this, tr("Error"),
                         tr("Some of the files failed to load.\n"
                            "Either we don't support their format, or they are broken.\n"
                            "You should remove them from the project.")
                             + QLatin1String("\n\n") + failures.join(QLatin1Char('\n')));
    return;
  }

  accept();
}

void ProjectFilesDialog::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  addToProjectBtn->setIcon(iconProvider.getIcon("right-arrow-inscribed"));
  removeFromProjectBtn->setIcon(iconProvider.getIcon("left-arrow-inscribed"));
}

/*====================== ProjectFilesDialog::FileList ====================*/

ProjectFilesDialog::FileList::FileList() = default;

ProjectFilesDialog::FileList::~FileList() = default;

void ProjectFilesDialog::FileList::clear() {
  if (m_items.empty()) {
    return;
  }
  beginRemoveRows(QModelIndex(), 0, static_cast<int>(m_items.size() - 1));
  m_items.clear();
  endRemoveRows();
}

void ProjectFilesDialog::FileList::remove(const QItemSelection& selection) {
  if (selection.isEmpty()) {
    return;
  }

  using Range = std::pair<int, int>;
  QVector<Range> sortedRanges;
  for (const auto& range : selection) {
    sortedRanges.push_back(Range(range.top(), range.bottom()));
  }

  std::sort(sortedRanges.begin(), sortedRanges.end(),
            [](const Range& lhs, const Range& rhs) { return lhs.first < rhs.first; });

  QVectorIterator<Range> it(sortedRanges);
  int rowsRemoved = 0;
  while (it.hasNext()) {
    const Range& range = it.next();
    const int first = range.first - rowsRemoved;
    const int last = range.second - rowsRemoved;
    beginRemoveRows(QModelIndex(), first, last);
    m_items.erase(m_items.begin() + first, m_items.begin() + (last + 1));
    endRemoveRows();
    rowsRemoved += last - first + 1;
  }
}  // ProjectFilesDialog::FileList::remove

int ProjectFilesDialog::FileList::rowCount(const QModelIndex&) const {
  return static_cast<int>(m_items.size());
}

QVariant ProjectFilesDialog::FileList::data(const QModelIndex& index, const int role) const {
  const Item& item = m_items[index.row()];
  switch (role) {
    case Qt::DisplayRole:
      return item.fileInfo().fileName();
    case Qt::ForegroundRole:
      switch (item.status()) {
        case Item::STATUS_DEFAULT:
          return QVariant();
        case Item::STATUS_LOAD_OK:
          return QBrush(QColor(0x00, 0xff, 0x00));
        case Item::STATUS_LOAD_FAILED:
          return QBrush(QColor(0xff, 0x00, 0x00));
      }
      break;
    case Qt::ToolTipRole:
      if (!item.loadError().isEmpty()) {
        return item.loadError();
      }
      break;
    default:
      break;
  }
  return QVariant();
}

Qt::ItemFlags ProjectFilesDialog::FileList::flags(const QModelIndex& index) const {
  return m_items[index.row()].flags();
}

void ProjectFilesDialog::FileList::prepareForLoadingFiles() {
  std::deque<int> itemIndexes;
  const auto numItems = static_cast<int>(m_items.size());
  for (int i = 0; i < numItems; ++i) {
    itemIndexes.push_back(i);
  }

  std::sort(itemIndexes.begin(), itemIndexes.end(),
            [&](int lhs, int rhs) { return ItemVisualOrdering()(m_items[lhs], m_items[rhs]); });

  m_itemsToLoad.swap(itemIndexes);
}

ProjectFilesDialog::FileList::LoadStatus ProjectFilesDialog::FileList::loadNextFile() {
  if (m_itemsToLoad.empty()) {
    return NO_MORE_FILES;
  }

  const int itemIdx = m_itemsToLoad.front();
  Item& item = m_items[itemIdx];
  std::vector<ImageMetadata> perPageMetadata;
  const QString filePath(item.fileInfo().absoluteFilePath());
  ImageLoadErrorCapture errorCapture;
  const ImageMetadataLoader::Status st = ImageMetadataLoader::load(
      filePath, [&](const ImageMetadata& metadata) { perPageMetadata.push_back(metadata); });

  LoadStatus status;

  if (st == ImageMetadataLoader::LOADED) {
    status = LOAD_OK;
    item.perPageMetadata().swap(perPageMetadata);
    item.setStatus(Item::STATUS_LOAD_OK);
    item.setLoadError(QString());
  } else {
    status = LOAD_FAILED;
    item.setStatus(Item::STATUS_LOAD_FAILED);
    const QStringList reasons = errorCapture.messages();
    item.setLoadError(
        reasons.isEmpty()
            ? QCoreApplication::translate("ImageLoader", "The file format is not supported, or the file is damaged.")
            : reasons.join(QLatin1Char('\n')));
  }
  const QModelIndex idx(index(itemIdx, 0));
  emit dataChanged(idx, idx);

  m_itemsToLoad.pop_front();
  return status;
}  // ProjectFilesDialog::FileList::loadNextFile

/*================= ProjectFilesDialog::SortedFileList ===================*/

ProjectFilesDialog::SortedFileList::SortedFileList(FileList& delegate) : m_delegate(delegate) {
  setSourceModel(delegate.model());
  setDynamicSortFilter(true);
  sort(0);
}

bool ProjectFilesDialog::SortedFileList::lessThan(const QModelIndex& lhs, const QModelIndex& rhs) const {
  const Item& lhsItem = m_delegate.item(lhs);
  const Item& rhsItem = m_delegate.item(rhs);
  return ItemVisualOrdering()(lhsItem, rhsItem);
}

/*=============== ProjectFilesDialog::ItemVisualOrdering =================*/

bool ProjectFilesDialog::ItemVisualOrdering::operator()(const Item& lhs, const Item& rhs) const {
  const bool lhsFailed = (lhs.status() == Item::STATUS_LOAD_FAILED);
  const bool rhsFailed = (rhs.status() == Item::STATUS_LOAD_FAILED);
  if (lhsFailed != rhsFailed) {
    // Failed ones go to the top.
    return lhsFailed;
  }
  return SmartFilenameOrdering()(lhs.fileInfo(), rhs.fileInfo());
}
