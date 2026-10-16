// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FixDpiDialog.h"

#include <QDir>
#include <QHeaderView>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <algorithm>

#include "ColorSchemeManager.h"
#include "StateIcon.h"
#include "Utils.h"

// Requests the resolution all images of an item have in common, as a QSize, undefined values
// included; an invalid QSize if they have different ones.
static const int DPI_ROLE = Qt::UserRole;
// Same as the one above, but only images that need fixing will be considered.
// FilterModel answers DPI_ROLE with this one while it shows only those images.
static const int NOT_OK_DPI_ROLE = Qt::UserRole + 1;

/**
 * The worse status of the two axes.  An undefined resolution counts as worse than a doubtful one
 * (too small or too large), which still lets the pages be processed.
 */
static ImageMetadata::DpiStatus worseDpiStatus(const ImageMetadata& metadata) {
  const ImageMetadata::DpiStatus horizontal = metadata.horizontalDpiStatus();
  const ImageMetadata::DpiStatus vertical = metadata.verticalDpiStatus();
  if ((horizontal == ImageMetadata::DPI_UNDEFINED) || (vertical == ImageMetadata::DPI_UNDEFINED)) {
    return ImageMetadata::DPI_UNDEFINED;
  }
  return (horizontal != ImageMetadata::DPI_OK) ? horizontal : vertical;
}

/** An image needs fixing if its resolution is undefined (red) or doubtful (yellow). */
static bool needsFixing(const ImageMetadata& metadata) {
  return worseDpiStatus(metadata) != ImageMetadata::DPI_OK;
}

static StateIcon::Color stateColor(const ImageMetadata& metadata) {
  switch (worseDpiStatus(metadata)) {
    case ImageMetadata::DPI_OK:
      return StateIcon::Green;
    case ImageMetadata::DPI_UNDEFINED:
      return StateIcon::Red;
    default:
      return StateIcon::Yellow;
  }
}


/**
 * Presents the images as a two-level tree, as the Relinking dialog does: folders (in the order
 * of their first image) with their images below.  Each page of a multi-page file is an image.
 * The second column shows the resolution of an image, or how many images of a folder need fixing.
 * A traffic light icon marks the state of an image, and of a folder that of its worst image.
 */
class FixDpiDialog::TreeModel : private QAbstractItemModel {
 public:
  explicit TreeModel(const std::vector<ImageFileInfo>& files);

  const std::vector<ImageFileInfo>& files() const { return m_files; }

  QAbstractItemModel* model() { return this; }

  /** Whether no image has an undefined resolution; doubtful ones don't count here. */
  bool allDpisDefined() const;

  int imageCount() const;

  int notOkImageCount() const;

  bool isVisibleForFilter(const QModelIndex& parent, int row) const;

  void applyDpiToSelection(Scope scope, const Dpi& dpi, const QItemSelection& selection);

  /** The pixel sizes of the images an item stands for, each once: of a folder or of one image. */
  std::vector<QSize> pixelSizesOf(const QModelIndex& index, Scope scope) const;

 private:
  struct Image {
    int fileIdx;
    int imageIdx;
  };

  struct Folder {
    QString path;
    std::vector<Image> images;
  };

  struct Tag {};

  int columnCount(const QModelIndex& parent) const override;

  int rowCount(const QModelIndex& parent) const override;

  QModelIndex index(int row, int column, const QModelIndex& parent) const override;

  QModelIndex parent(const QModelIndex& index) const override;

  QVariant data(const QModelIndex& index, int role) const override;

  const ImageMetadata& metadataOf(const Image& image) const;

  /**
   * The images of a folder or the single image an item stands for.
   * With NOT_OK, those of a folder are limited to the ones that need fixing.
   */
  std::vector<Image> imagesOf(const QModelIndex& index, Scope scope) const;

  int notOkImageCount(const Folder& folder) const;

  StateIcon::Color stateColor(const Folder& folder) const;

  QSize commonDpi(const QModelIndex& index, Scope scope) const;

  static QString dpiText(const ImageMetadata& metadata);

  static Tag m_folderNodeId;

  std::vector<ImageFileInfo> m_files;
  std::vector<Folder> m_folders;
};


class FixDpiDialog::FilterModel : private QSortFilterProxyModel {
 public:
  explicit FilterModel(TreeModel& delegate);

  QAbstractProxyModel* model() { return this; }

  bool needFixingOnly() const { return m_needFixingOnly; }

  /** Shows only the folders and images that need fixing, or all of them. */
  void setNeedFixingOnly(bool needFixingOnly);

 private:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

  QVariant data(const QModelIndex& index, int role) const override;

  TreeModel& m_delegate;
  bool m_needFixingOnly;
};


FixDpiDialog::FixDpiDialog(const std::vector<ImageFileInfo>& files, QWidget* parent)
    : QDialog(parent), m_pages(std::make_unique<TreeModel>(files)), m_filter(std::make_unique<FilterModel>(*m_pages)) {
  setupUi(this);

  // Lists of the usual values that also take any other value typed in, as in the Margins step.
  for (QComboBox* field : {xDpi, yDpi}) {
    for (const int dpi : {300, 400, 600, 1200}) {
      field->addItem(core::Utils::dpiText(dpi));
    }
    field->setInsertPolicy(QComboBox::NoInsert);
    field->setValidator(core::Utils::createDpiValidator(field));
    core::Utils::setDpiFieldWidth(field);
  }

  m_normalPalette = xDpi->lineEdit()->palette();
  m_errorPalette = m_normalPalette;
  const QColor errorTextColor = ColorSchemeManager::instance().getColorParam("FixDpiDialogErrorText", QColor(Qt::red));
  m_errorPalette.setColor(QPalette::Text, errorTextColor);

  // Opens with the pages that need fixing, if there are any.
  m_filter->setNeedFixingOnly(m_pages->notOkImageCount() > 0);
  needFixingOnlyCB->setChecked(m_filter->needFixingOnly());
  treeView->setModel(m_filter->model());
  treeView->header()->setStretchLastSection(false);
  treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  treeView->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  treeView->expandAll();
  updateSummary();

  connect(needFixingOnlyCB, &QCheckBox::toggled, this, &FixDpiDialog::needFixingOnlyToggled);
  connect(treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &FixDpiDialog::selectionChanged);

  connect(xDpi, &QComboBox::activated, this, &FixDpiDialog::dpiChosenFromList);
  connect(yDpi, &QComboBox::activated, this, &FixDpiDialog::dpiChosenFromList);
  connect(xDpi->lineEdit(), &QLineEdit::textEdited, this, &FixDpiDialog::dpiValueChanged);
  connect(yDpi->lineEdit(), &QLineEdit::textEdited, this, &FixDpiDialog::dpiValueChanged);

  connect(applyBtn, &QAbstractButton::clicked, this, &FixDpiDialog::applyClicked);

  enableDisableOkButton();
}

FixDpiDialog::~FixDpiDialog() = default;

const std::vector<ImageFileInfo>& FixDpiDialog::files() const {
  return m_pages->files();
}

void FixDpiDialog::needFixingOnlyToggled(const bool checked) {
  // Selected items that are hidden now are deselected; the others stay selected.
  // Those show and get only the resolution of their pages that need fixing while the box is on.
  m_filter->setNeedFixingOnly(checked);
  treeView->expandAll();
  updateDpiFromSelection();
}

void FixDpiDialog::selectionChanged(const QItemSelection&) {
  // The argument holds only the newly selected items, not the whole selection.
  updateDpiFromSelection();
}

void FixDpiDialog::dpiChosenFromList() {
  auto* chosen = qobject_cast<QComboBox*>(sender());
  if (!chosen) {
    return;
  }
  // Both values are almost always the same, so a value chosen from the list sets both while
  // they are.  Different values stay different; typing changes only the one field.
  QComboBox* other = (chosen == xDpi) ? yDpi : xDpi;
  if (m_initialDpi.isEmpty() || (m_initialDpi.width() == m_initialDpi.height())) {
    other->setEditText(chosen->currentText());
  }
  dpiValueChanged();
}

Dpi FixDpiDialog::enteredDpi() const {
  bool xOk = false;
  bool yOk = false;
  const int horizontal = core::Utils::dpiFromText(xDpi->currentText(), &xOk);
  const int vertical = core::Utils::dpiFromText(yDpi->currentText(), &yOk);
  return Dpi(xOk ? horizontal : 0, yOk ? vertical : 0);
}

void FixDpiDialog::dpiValueChanged() {
  const Dpi dpi(enteredDpi());

  // The resolution must suit every selected pixel size; a field shows the first problem found.
  ImageMetadata::DpiStatus horizontalStatus = ImageMetadata::DPI_OK;
  ImageMetadata::DpiStatus verticalStatus = ImageMetadata::DPI_OK;
  bool dpiOK = !m_selectedPixelSizes.empty();
  for (const QSize& pixelSize : m_selectedPixelSizes) {
    const ImageMetadata metadata(pixelSize, dpi);
    if (horizontalStatus == ImageMetadata::DPI_OK) {
      horizontalStatus = metadata.horizontalDpiStatus();
    }
    if (verticalStatus == ImageMetadata::DPI_OK) {
      verticalStatus = metadata.verticalDpiStatus();
    }
    dpiOK = dpiOK && metadata.isDpiOK();
  }

  decorateDpiInputField(xDpi, horizontalStatus);
  decorateDpiInputField(yDpi, verticalStatus);

  applyBtn->setEnabled(dpiOK && (dpi.toSize() != m_initialDpi));
}

void FixDpiDialog::applyClicked() {
  const Dpi dpi(enteredDpi());
  const QItemSelection selection(m_filter->model()->mapSelectionToSource(treeView->selectionModel()->selection()));
  m_pages->applyDpiToSelection(scope(), dpi, selection);

  updateDpiFromSelection();
  updateSummary();
  enableDisableOkButton();
}

FixDpiDialog::Scope FixDpiDialog::scope() const {
  // While only the pages that need fixing are shown, a folder passes the resolution only to those.
  return m_filter->needFixingOnly() ? NOT_OK : ALL;
}

void FixDpiDialog::updateSummary() {
  imagesSummaryLabel->setText(tr("%1 of %2 need fixing").arg(m_pages->notOkImageCount()).arg(m_pages->imageCount()));
}

void FixDpiDialog::enableDisableOkButton() {
  const bool enable = m_pages->allDpisDefined();
  buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enable);
}

/**
 * The fields show the resolution if all selected items have the same one, else they are empty.
 */
void FixDpiDialog::updateDpiFromSelection() {
  const QModelIndexList indexes(treeView->selectionModel()->selectedRows());
  const bool enabled = !indexes.isEmpty();
  // applyBtn is managed elsewhere.
  for (QWidget* widget : {static_cast<QWidget*>(dpiLabel), static_cast<QWidget*>(xDpi),
                          static_cast<QWidget*>(dpiTimesLabel), static_cast<QWidget*>(yDpi)}) {
    widget->setEnabled(enabled);
  }

  m_selectedPixelSizes.clear();
  QSize commonDpi;
  bool sameDpi = true;
  for (const QModelIndex& index : indexes) {
    for (const QSize& pixelSize : m_pages->pixelSizesOf(m_filter->model()->mapToSource(index), scope())) {
      if (std::find(m_selectedPixelSizes.begin(), m_selectedPixelSizes.end(), pixelSize)
          == m_selectedPixelSizes.end()) {
        m_selectedPixelSizes.push_back(pixelSize);
      }
    }

    // FilterModel may replace DPI_ROLE with NOT_OK_DPI_ROLE.
    // An invalid size means "different ones" and leaves the fields empty.
    const QSize dpi(index.data(DPI_ROLE).toSize());
    if (index == indexes.front()) {
      commonDpi = dpi;
    } else if (dpi != commonDpi) {
      sameDpi = false;
    }
  }

  if (enabled && sameDpi) {
    setDpiForm(Dpi(commonDpi));
  } else {
    resetDpiForm();
  }
}

void FixDpiDialog::resetDpiForm() {
  m_initialDpi = QSize(0, 0);
  xDpi->setEditText(QString());
  yDpi->setEditText(QString());
  dpiValueChanged();
}

void FixDpiDialog::setDpiForm(const Dpi& dpi) {
  // An undefined value (1 or less) leaves its field empty, the other one still shows its value.
  m_initialDpi = dpi.isNull() ? QSize(0, 0) : dpi.toSize();
  xDpi->setEditText((dpi.horizontal() > 1) ? core::Utils::dpiText(dpi.horizontal()) : QString());
  yDpi->setEditText((dpi.vertical() > 1) ? core::Utils::dpiText(dpi.vertical()) : QString());
  dpiValueChanged();
}

void FixDpiDialog::decorateDpiInputField(QComboBox* field, ImageMetadata::DpiStatus dpiStatus) const {
  if (dpiStatus == ImageMetadata::DPI_OK) {
    field->lineEdit()->setPalette(m_normalPalette);
  } else {
    field->lineEdit()->setPalette(m_errorPalette);
  }

  switch (dpiStatus) {
    case ImageMetadata::DPI_OK:
    case ImageMetadata::DPI_UNDEFINED:
      field->setToolTip(QString());
      break;
    case ImageMetadata::DPI_TOO_LARGE:
      field->setToolTip(tr("DPI is too large and most likely wrong."));
      break;
    case ImageMetadata::DPI_TOO_SMALL:
      field->setToolTip(
          tr("DPI is too small. Even if it's correct, you are not going to get acceptable results with it."));
      break;
    case ImageMetadata::DPI_TOO_SMALL_FOR_THIS_PIXEL_SIZE:
      field->setToolTip(
          tr("DPI is too small for this pixel size. Such combination would probably lead to out of "
             "memory errors."));
      break;
  }
}

/*====================== FixDpiDialog::TreeModel ======================*/

FixDpiDialog::TreeModel::Tag FixDpiDialog::TreeModel::m_folderNodeId;

FixDpiDialog::TreeModel::TreeModel(const std::vector<ImageFileInfo>& files) : m_files(files) {
  const auto numFiles = static_cast<int>(m_files.size());
  for (int i = 0; i < numFiles; ++i) {
    const QString path(m_files[i].fileInfo().absolutePath());
    auto folder = std::find_if(m_folders.begin(), m_folders.end(),
                               [&path](const Folder& folder) { return folder.path == path; });
    if (folder == m_folders.end()) {
      m_folders.push_back(Folder{path, {}});
      folder = m_folders.end() - 1;
    }
    const auto numImages = static_cast<int>(m_files[i].imageInfo().size());
    for (int j = 0; j < numImages; ++j) {
      folder->images.push_back(Image{i, j});
    }
  }
}

bool FixDpiDialog::TreeModel::allDpisDefined() const {
  return std::all_of(m_folders.begin(), m_folders.end(), [this](const Folder& folder) {
    return std::all_of(folder.images.begin(), folder.images.end(),
                       [this](const Image& image) { return metadataOf(image).isDpiOK(); });
  });
}

int FixDpiDialog::TreeModel::imageCount() const {
  int count = 0;
  for (const Folder& folder : m_folders) {
    count += static_cast<int>(folder.images.size());
  }
  return count;
}

int FixDpiDialog::TreeModel::notOkImageCount() const {
  int count = 0;
  for (const Folder& folder : m_folders) {
    count += notOkImageCount(folder);
  }
  return count;
}

int FixDpiDialog::TreeModel::notOkImageCount(const Folder& folder) const {
  return static_cast<int>(std::count_if(folder.images.begin(), folder.images.end(),
                                        [this](const Image& image) { return needsFixing(metadataOf(image)); }));
}

StateIcon::Color FixDpiDialog::TreeModel::stateColor(const Folder& folder) const {
  StateIcon::Color color = StateIcon::Green;
  for (const Image& image : folder.images) {
    switch (::stateColor(metadataOf(image))) {
      case StateIcon::Red:
        return StateIcon::Red;
      case StateIcon::Yellow:
        color = StateIcon::Yellow;
        break;
      default:
        break;
    }
  }
  return color;
}

bool FixDpiDialog::TreeModel::isVisibleForFilter(const QModelIndex& parent, const int row) const {
  if (!parent.isValid()) {
    // A folder.
    return notOkImageCount(m_folders[row]) > 0;
  }
  // An image.
  return needsFixing(metadataOf(m_folders[parent.row()].images[row]));
}

void FixDpiDialog::TreeModel::applyDpiToSelection(const Scope scope, const Dpi& dpi, const QItemSelection& selection) {
  // The tree doesn't change its rows here, so the indexes stay valid.
  for (const QModelIndex& index : selection.indexes()) {
    if (index.column() != 0) {
      continue;
    }
    for (const Image& image : imagesOf(index, scope)) {
      ImageMetadata& metadata = m_files[image.fileIdx].imageInfo()[image.imageIdx];
      metadata = ImageMetadata(metadata.size(), dpi);
    }
  }

  // Images, their folders and the counts of other folders may have changed.
  const int numFolders = rowCount(QModelIndex());
  for (int i = 0; i < numFolders; ++i) {
    const QModelIndex folderIndex(index(i, 0, QModelIndex()));
    const int numImages = rowCount(folderIndex);
    if (numImages > 0) {
      emit dataChanged(index(0, 0, folderIndex), index(numImages - 1, 1, folderIndex));
    }
  }
  if (numFolders > 0) {
    emit dataChanged(index(0, 0, QModelIndex()), index(numFolders - 1, 1, QModelIndex()));
  }
}

std::vector<QSize> FixDpiDialog::TreeModel::pixelSizesOf(const QModelIndex& index, const Scope scope) const {
  std::vector<QSize> sizes;
  for (const Image& image : imagesOf(index, scope)) {
    const QSize& size = metadataOf(image).size();
    if (std::find(sizes.begin(), sizes.end(), size) == sizes.end()) {
      sizes.push_back(size);
    }
  }
  return sizes;
}

const ImageMetadata& FixDpiDialog::TreeModel::metadataOf(const Image& image) const {
  return m_files[image.fileIdx].imageInfo()[image.imageIdx];
}

std::vector<FixDpiDialog::TreeModel::Image> FixDpiDialog::TreeModel::imagesOf(const QModelIndex& index,
                                                                              const Scope scope) const {
  const QModelIndex parent(index.parent());
  if (parent.isValid()) {
    // An image on its own.
    return {m_folders[parent.row()].images[index.row()]};
  }

  std::vector<Image> images;
  for (const Image& image : m_folders[index.row()].images) {
    if ((scope == ALL) || needsFixing(metadataOf(image))) {
      images.push_back(image);
    }
  }
  return images;
}

QSize FixDpiDialog::TreeModel::commonDpi(const QModelIndex& index, const Scope scope) const {
  const std::vector<Image> images(imagesOf(index, scope));
  if (images.empty()) {
    return QSize();
  }

  // Not Dpi::toSize(), which drops a resolution with only one axis defined.
  const auto sizeOf = [this](const Image& image) {
    const Dpi& dpi = metadataOf(image).dpi();
    return QSize(dpi.horizontal(), dpi.vertical());
  };
  const QSize dpi(sizeOf(images.front()));
  for (const Image& image : images) {
    if (sizeOf(image) != dpi) {
      return QSize();
    }
  }
  return dpi;
}

QString FixDpiDialog::TreeModel::dpiText(const ImageMetadata& metadata) {
  const Dpi& dpi = metadata.dpi();
  const QString text(QString("%1 %2 %3 dpi").arg(dpi.horizontal()).arg(QChar(0x00D7)).arg(dpi.vertical()));
  switch (worseDpiStatus(metadata)) {
    case ImageMetadata::DPI_OK:
      return text;
    case ImageMetadata::DPI_UNDEFINED:
      return FixDpiDialog::tr("undefined");
    case ImageMetadata::DPI_TOO_LARGE:
      return FixDpiDialog::tr("%1, too large").arg(text);
    case ImageMetadata::DPI_TOO_SMALL:
    case ImageMetadata::DPI_TOO_SMALL_FOR_THIS_PIXEL_SIZE:
      return FixDpiDialog::tr("%1, too small").arg(text);
  }
  return text;
}

int FixDpiDialog::TreeModel::columnCount(const QModelIndex& parent) const {
  return 2;
}

int FixDpiDialog::TreeModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    // Folders.
    return static_cast<int>(m_folders.size());
  } else if ((parent.internalPointer() == &m_folderNodeId) && (parent.column() == 0)) {
    // Images within a folder.
    return static_cast<int>(m_folders[parent.row()].images.size());
  } else {
    // Children of an image.
    return 0;
  }
}

QModelIndex FixDpiDialog::TreeModel::index(const int row, const int column, const QModelIndex& parent) const {
  if (!parent.isValid()) {
    // A folder.
    return createIndex(row, column, &m_folderNodeId);
  } else if (parent.internalPointer() == &m_folderNodeId) {
    // An image within a folder.
    return createIndex(row, column, (void*) &m_folders[parent.row()]);
  }
  return QModelIndex();
}

QModelIndex FixDpiDialog::TreeModel::parent(const QModelIndex& index) const {
  const void* const ptr = index.internalPointer();

  if (!index.isValid() || (ptr == &m_folderNodeId)) {
    // Folder -> tree root.
    return QModelIndex();
  }
  // Image -> folder.
  const auto* folder = static_cast<const Folder*>(ptr);
  return createIndex(static_cast<int>(folder - &m_folders[0]), 0, &m_folderNodeId);
}

QVariant FixDpiDialog::TreeModel::data(const QModelIndex& index, const int role) const {
  if (!index.isValid()) {
    // Should not happen.
    return QVariant();
  }

  if (role == DPI_ROLE) {
    return commonDpi(index, ALL);
  } else if (role == NOT_OK_DPI_ROLE) {
    return commonDpi(index, NOT_OK);
  } else if (role == Qt::DecorationRole) {
    if (index.column() != 0) {
      return QVariant();
    }
  } else if ((role != Qt::DisplayRole) && (role != Qt::ToolTipRole)) {
    return QVariant();
  }

  if (index.internalPointer() == &m_folderNodeId) {
    // Folder.
    const Folder& folder = m_folders[index.row()];
    if (role == Qt::DecorationRole) {
      return StateIcon::pixmap(stateColor(folder));
    }
    if (index.column() == 0) {
      return QDir::toNativeSeparators(folder.path);
    }
    return FixDpiDialog::tr("%1 of %2 need fixing")
        .arg(notOkImageCount(folder))
        .arg(static_cast<int>(folder.images.size()));
  }

  // Image.
  const auto* folder = static_cast<const Folder*>(index.internalPointer());
  const Image& image = folder->images[index.row()];
  if (role == Qt::DecorationRole) {
    return StateIcon::pixmap(::stateColor(metadataOf(image)));
  }
  if (index.column() == 1) {
    return dpiText(metadataOf(image));
  }
  const ImageFileInfo& file = m_files[image.fileIdx];
  if (role == Qt::ToolTipRole) {
    return QDir::toNativeSeparators(file.fileInfo().absoluteFilePath());
  }
  const QString fname(file.fileInfo().fileName());
  if (file.imageInfo().size() == 1) {
    return fname;
  }
  return FixDpiDialog::tr("%1 (page %2)").arg(fname).arg(image.imageIdx + 1);
}  // FixDpiDialog::TreeModel::data

/*====================== FixDpiDialog::FilterModel ======================*/

FixDpiDialog::FilterModel::FilterModel(TreeModel& delegate) : m_delegate(delegate), m_needFixingOnly(false) {
  setDynamicSortFilter(true);
  setSourceModel(delegate.model());
}

void FixDpiDialog::FilterModel::setNeedFixingOnly(const bool needFixingOnly) {
  if (m_needFixingOnly != needFixingOnly) {
    m_needFixingOnly = needFixingOnly;
    invalidateFilter();
  }
}

bool FixDpiDialog::FilterModel::filterAcceptsRow(const int sourceRow, const QModelIndex& sourceParent) const {
  return !m_needFixingOnly || m_delegate.isVisibleForFilter(sourceParent, sourceRow);
}

QVariant FixDpiDialog::FilterModel::data(const QModelIndex& index, int role) const {
  if (m_needFixingOnly && (role == DPI_ROLE)) {
    role = NOT_OK_DPI_ROLE;
  }
  return QSortFilterProxyModel::data(index, role);
}
