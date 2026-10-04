// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportDialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <tuple>
#include <utility>

#include "ApplicationSettings.h"
#include "ImageId.h"
#include "OcrEngine.h"
#include "OcrLanguages.h"
#include "PdfExportJob.h"
#include "TessdataDownloadDialog.h"
#include "ThumbnailLoadResult.h"

class PdfExportDialog::ThumbnailHandler : public ThumbnailPixmapCache::CompletionHandler {
 public:
  ThumbnailHandler(PdfExportDialog* dialog, const int entryIndex) : m_dialog(dialog), m_entryIndex(entryIndex) {}

  void operator()(const ThumbnailLoadResult& result) override { m_dialog->thumbnailLoaded(m_entryIndex, result); }

 private:
  PdfExportDialog* m_dialog;
  int m_entryIndex;
};


PdfExportDialog::PdfExportDialog(std::vector<Entry> entries,
                                 std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                                 const QString& defaultFile,
                                 QWidget* parent)
    : QDialog(parent),
      m_entries(std::move(entries)),
      m_thumbnailCache(std::move(thumbnailCache)),
      m_thumbnailHandlers(m_entries.size()) {
  setWindowTitle(tr("Create PDF"));

  // Only the file headers are read, so this is quick even for many pages.
  QApplication::setOverrideCursor(Qt::WaitCursor);
  for (Entry& entry : m_entries) {
    entry.page.analyze();
  }
  QApplication::restoreOverrideCursor();

  const ApplicationSettings& settings = ApplicationSettings::getInstance();

  auto* mainLayout = new QVBoxLayout(this);
  auto* contentLayout = new QHBoxLayout;
  mainLayout->addLayout(contentLayout, 1);

  // Page list.
  auto* pagesGroup = new QGroupBox(tr("Pages"));
  auto* pagesLayout = new QVBoxLayout(pagesGroup);
  m_pageList = new QListWidget;
  m_pageList->setIconSize(QSize(64, 90));
  m_pageList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_pageList->setDragDropMode(QAbstractItemView::InternalMove);
  m_pageList->setDefaultDropAction(Qt::MoveAction);
  m_pageList->setMinimumWidth(380);
  pagesLayout->addWidget(m_pageList, 1);

  m_selectionLabel = new QLabel;
  pagesLayout->addWidget(m_selectionLabel);

  auto* listButtons = new QHBoxLayout;
  m_allButton = new QPushButton(tr("All"));
  m_noneButton = new QPushButton(tr("None"));
  m_upButton = new QPushButton(tr("Move up"));
  m_downButton = new QPushButton(tr("Move down"));
  m_allButton->setToolTip(tr("Include all output pages in the PDF."));
  m_noneButton->setToolTip(tr("Exclude all pages from the PDF."));
  m_upButton->setToolTip(tr("Move the selected pages up.  Pages can also be moved with drag and drop."));
  m_downButton->setToolTip(tr("Move the selected pages down.  Pages can also be moved with drag and drop."));
  listButtons->addWidget(m_allButton);
  listButtons->addWidget(m_noneButton);
  listButtons->addStretch(1);
  listButtons->addWidget(m_upButton);
  listButtons->addWidget(m_downButton);
  pagesLayout->addLayout(listButtons);
  contentLayout->addWidget(pagesGroup, 1);

  // Options.
  auto* optionsColumn = new QVBoxLayout;
  auto* optionsGroup = new QGroupBox(tr("Options"));
  auto* optionsLayout = new QFormLayout(optionsGroup);

  m_jpegQuality = new QSpinBox;
  m_jpegQuality->setRange(10, 100);
  m_jpegQuality->setValue(settings.getPdfJpegQuality());
  m_jpegQuality->setToolTip(
      tr("Higher values give better pictures and larger files.  Applies to grayscale and color pages and to the "
         "pictures of pages with split output."));
  optionsLayout->addRow(tr("JPEG quality:"), m_jpegQuality);

  m_backgroundScale = new QComboBox;
  m_backgroundScale->addItem(tr("Full"), 1);
  m_backgroundScale->addItem(tr("Half"), 2);
  m_backgroundScale->addItem(tr("One third"), 3);
  m_backgroundScale->setCurrentIndex(std::max(0, m_backgroundScale->findData(settings.getPdfBackgroundScale())));
  m_backgroundScale->setToolTip(
      tr("Resolution of the pictures of pages with split output, relative to the output resolution.  The text "
         "is always stored at full resolution."));
  optionsLayout->addRow(tr("Picture resolution:"), m_backgroundScale);

  m_bitonalCompression = new QComboBox;
  m_bitonalCompression->addItem(tr("JBIG2 (lossless, smaller)"), true);
  m_bitonalCompression->addItem(tr("CCITT G4 (for older programs)"), false);
  m_bitonalCompression->setCurrentIndex(settings.isPdfJbig2Enabled() ? 0 : 1);
  m_bitonalCompression->setToolTip(
      tr("Compression of black and white pages and of the text of pages with split output.  Both are lossless; "
         "JBIG2 files are about a third smaller.  Some very old PDF programs can't show JBIG2."));
  optionsLayout->addRow(tr("Black and white:"), m_bitonalCompression);

  m_openAfterCreation = new QCheckBox(tr("Open the PDF after creating it"));
  m_openAfterCreation->setChecked(settings.isPdfOpenAfterCreationEnabled());
  optionsLayout->addRow(m_openAfterCreation);

  auto* hint = new QLabel(
      tr("Black and white pages are stored losslessly.  Pages with split output store their pictures as JPEG and "
         "their text losslessly on top.  All other pages are stored as JPEG."));
  hint->setWordWrap(true);
  optionsLayout->addRow(hint);

  optionsColumn->addWidget(optionsGroup);

#ifdef ENABLE_OCR
  // Text recognition.  The group box's check box turns it on and off.
  m_ocrGroup = new QGroupBox(tr("Text recognition (OCR)"));
  m_ocrGroup->setCheckable(true);
  m_ocrGroup->setChecked(settings.isPdfOcrEnabled());
  m_ocrGroup->setToolTip(
      tr("Adds an invisible text layer, so the text of the PDF can be searched, selected and copied."));
  auto* ocrLayout = new QVBoxLayout(m_ocrGroup);
  ocrLayout->addWidget(new QLabel(tr("Languages:")));
  m_languageList = new QListWidget;
  m_languageList->setSelectionMode(QAbstractItemView::NoSelection);
  m_languageList->setToolTip(tr("Tick the languages of the text.  Several languages can be ticked."));
  ocrLayout->addWidget(m_languageList, 1);

  QStringList dirs;
  for (const QString& dir : OcrLanguages::searchDirs()) {
    dirs.push_back(QDir::toNativeSeparators(dir));
  }
  m_noLanguagesLabel = new QLabel(tr("No language files were found.  Download them with \"More languages\", or "
                                     "put *.traineddata files (from tessdata_best) into one of these folders:")
                                  + QStringLiteral("\n") + dirs.join('\n'));
  m_noLanguagesLabel->setWordWrap(true);
  m_noLanguagesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ocrLayout->addWidget(m_noLanguagesLabel);

  auto* moreLanguagesButton = new QPushButton(tr("More languages ..."));
  moreLanguagesButton->setToolTip(tr("Download more languages from the internet."));
  auto* moreLanguagesRow = new QHBoxLayout;
  moreLanguagesRow->addStretch(1);
  moreLanguagesRow->addWidget(moreLanguagesButton);
  ocrLayout->addLayout(moreLanguagesRow);
  connect(moreLanguagesButton, &QPushButton::clicked, this, &PdfExportDialog::downloadLanguages);

  fillLanguageList(settings.getPdfOcrLanguages());

  auto* layoutRow = new QFormLayout;
  m_pageLayout = new QComboBox;
  m_pageLayout->addItem(tr("Automatic"), static_cast<int>(OcrEngine::AUTOMATIC_LAYOUT));
  m_pageLayout->addItem(tr("Single column"), static_cast<int>(OcrEngine::SINGLE_COLUMN));
  m_pageLayout->addItem(tr("Single block of text"), static_cast<int>(OcrEngine::SINGLE_BLOCK));
  m_pageLayout->setCurrentIndex(std::max(0, m_pageLayout->findData(settings.getPdfOcrPageLayout())));
  m_pageLayout->setToolTip(
      tr("How the text is arranged on the pages.  \"Automatic\" detects columns, pictures and captions.  If "
         "the text of a page comes out in the wrong order, try \"Single column\"."));
  layoutRow->addRow(tr("Page layout:"), m_pageLayout);
  ocrLayout->addLayout(layoutRow);

  auto* ocrHint = new QLabel(tr("Text recognition takes a few seconds per page."));
  ocrHint->setWordWrap(true);
  ocrLayout->addWidget(ocrHint);

  optionsColumn->addWidget(m_ocrGroup, 1);
#endif

  optionsColumn->addStretch(1);
  contentLayout->addLayout(optionsColumn);

  // Target file.
  auto* fileLayout = new QHBoxLayout;
  fileLayout->addWidget(new QLabel(tr("PDF file:")));
  m_fileEdit = new QLineEdit(QDir::toNativeSeparators(defaultFile));
  fileLayout->addWidget(m_fileEdit, 1);
  m_browseButton = new QPushButton(tr("Browse ..."));
  fileLayout->addWidget(m_browseButton);
  mainLayout->addLayout(fileLayout);

  // Progress.
  m_progressBar = new QProgressBar;
  m_progressBar->setRange(0, 1);
  m_progressBar->setValue(0);
  m_progressBar->setFormat("%v / %m");
  mainLayout->addWidget(m_progressBar);
  m_statusLabel = new QLabel;
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mainLayout->addWidget(m_statusLabel);

  auto* dialogButtons = new QHBoxLayout;
  dialogButtons->addStretch(1);
  m_createButton = new QPushButton(tr("Create PDF"));
  m_createButton->setDefault(true);
  m_closeButton = new QPushButton(tr("Close"));
  dialogButtons->addWidget(m_createButton);
  dialogButtons->addWidget(m_closeButton);
  mainLayout->addLayout(dialogButtons);

  // Fill the page list.
  for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
    const PdfExportPage& page = m_entries[i].page;
    auto* item = new QListWidgetItem(m_pageList);
    item->setData(Qt::UserRole, i);
    QString secondLine = kindText(page);
    if (!page.warning().isEmpty()) {
      secondLine += QStringLiteral("  ") + QChar(0x26A0);
    }
    item->setText(m_entries[i].label + '\n' + secondLine);
    item->setToolTip(page.warning().isEmpty() ? kindText(page) : page.warning());
    item->setIcon(makeIcon(QPixmap()));
    if (page.kind() == PdfExportPage::MISSING) {
      // Shown greyed out and can't be selected.
      item->setFlags(Qt::NoItemFlags);
    } else {
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
      item->setCheckState(Qt::Checked);
      requestThumbnail(i);
    }
  }

  connect(m_pageList, &QListWidget::itemChanged, this, &PdfExportDialog::updateControls);
  connect(m_pageList, &QListWidget::itemSelectionChanged, this, &PdfExportDialog::updateControls);
  connect(m_allButton, &QPushButton::clicked, this, &PdfExportDialog::selectAll);
  connect(m_noneButton, &QPushButton::clicked, this, &PdfExportDialog::selectNone);
  connect(m_upButton, &QPushButton::clicked, this, &PdfExportDialog::moveUp);
  connect(m_downButton, &QPushButton::clicked, this, &PdfExportDialog::moveDown);
  connect(m_browseButton, &QPushButton::clicked, this, &PdfExportDialog::browse);
  connect(m_createButton, &QPushButton::clicked, this, &PdfExportDialog::startExport);
  connect(m_closeButton, &QPushButton::clicked, this, &PdfExportDialog::reject);

  updateControls();
  resize(900, 720);
}

PdfExportDialog::~PdfExportDialog() {
  if (m_thread) {
    m_job->cancel();
    m_thread->wait();
    delete m_thread;
  }
}

void PdfExportDialog::reject() {
  if (m_thread) {
    // Closing while running cancels the export; the dialog stays open to show the result.
    m_job->cancel();
    m_statusLabel->setText(tr("Cancelling ..."));
    m_closeButton->setEnabled(false);
    return;
  }
  QDialog::reject();
}

void PdfExportDialog::selectAll() {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      item->setCheckState(Qt::Checked);
    }
  }
}

void PdfExportDialog::selectNone() {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      item->setCheckState(Qt::Unchecked);
    }
  }
}

void PdfExportDialog::moveUp() {
  std::vector<int> rows;
  for (QListWidgetItem* item : m_pageList->selectedItems()) {
    rows.push_back(m_pageList->row(item));
  }
  std::sort(rows.begin(), rows.end());
  if (rows.empty() || (rows.front() == 0)) {
    return;
  }
  for (const int row : rows) {
    QListWidgetItem* item = m_pageList->takeItem(row);
    m_pageList->insertItem(row - 1, item);
  }
  m_pageList->clearSelection();
  for (const int row : rows) {
    m_pageList->item(row - 1)->setSelected(true);
  }
  m_pageList->scrollToItem(m_pageList->item(rows.front() - 1));
}

void PdfExportDialog::moveDown() {
  std::vector<int> rows;
  for (QListWidgetItem* item : m_pageList->selectedItems()) {
    rows.push_back(m_pageList->row(item));
  }
  std::sort(rows.begin(), rows.end(), std::greater<>());
  if (rows.empty() || (rows.front() == m_pageList->count() - 1)) {
    return;
  }
  for (const int row : rows) {
    QListWidgetItem* item = m_pageList->takeItem(row);
    m_pageList->insertItem(row + 1, item);
  }
  m_pageList->clearSelection();
  for (const int row : rows) {
    m_pageList->item(row + 1)->setSelected(true);
  }
  m_pageList->scrollToItem(m_pageList->item(rows.front() + 1));
}

void PdfExportDialog::browse() {
  QString path = QFileDialog::getSaveFileName(this, tr("Save PDF as"), m_fileEdit->text(), tr("PDF files (*.pdf)"));
  if (path.isEmpty()) {
    return;
  }
  if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
    // The file dialog has already asked about replacing an existing file.
    m_overwriteConfirmed = QDir::cleanPath(path);
  } else {
    path += ".pdf";
  }
  m_fileEdit->setText(QDir::toNativeSeparators(path));
}

void PdfExportDialog::startExport() {
  if (m_thread) {
    return;
  }

  QString path = QDir::fromNativeSeparators(m_fileEdit->text().trimmed());
  if (path.isEmpty()) {
    QMessageBox::warning(this, windowTitle(), tr("Please enter the file name of the PDF."));
    return;
  }
  if (!path.endsWith(".pdf", Qt::CaseInsensitive)) {
    path += ".pdf";
  }
  const QFileInfo fileInfo(path);
  if (fileInfo.isRelative()) {
    QMessageBox::warning(this, windowTitle(), tr("Please enter the complete path of the PDF, including the folder."));
    return;
  }
  path = QDir::cleanPath(fileInfo.absoluteFilePath());
  m_fileEdit->setText(QDir::toNativeSeparators(path));
  if (!fileInfo.absoluteDir().exists()) {
    QMessageBox::warning(this, windowTitle(),
                         tr("The folder %1 doesn't exist.").arg(QDir::toNativeSeparators(fileInfo.absolutePath())));
    return;
  }
  if (fileInfo.isDir()) {
    QMessageBox::warning(this, windowTitle(), tr("%1 is a folder.").arg(QDir::toNativeSeparators(path)));
    return;
  }
  if (fileInfo.exists() && (path != m_overwriteConfirmed)) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, windowTitle(),
        tr("The file %1 already exists.\nDo you want to replace it?").arg(QDir::toNativeSeparators(path)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }

  std::vector<PdfExportPage> pages;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      pages.push_back(m_entries[item->data(Qt::UserRole).toInt()].page);
    }
  }
  if (pages.empty()) {
    QMessageBox::information(this, windowTitle(), tr("Please tick the pages to include in the PDF."));
    return;
  }

  PdfExportOptions options;
  options.jpegQuality = m_jpegQuality->value();
  options.backgroundScale = m_backgroundScale->currentData().toInt();
  options.jbig2 = m_bitonalCompression->currentData().toBool();
  options.threadCount = PdfExportJob::defaultThreadCount();

  ApplicationSettings& settings = ApplicationSettings::getInstance();
#ifdef ENABLE_OCR
  const QStringList languages = checkedLanguages();
  if (m_ocrGroup->isChecked()) {
    if (languages.isEmpty()) {
      QMessageBox::warning(this, windowTitle(),
                           tr("Please tick at least one language for the text recognition, or turn it off."));
      return;
    }
    QString dataDir = OcrLanguages::commonDir(languages);
    if (dataDir.isEmpty()) {
      // Tesseract needs all languages in one folder, so the missing ones are copied
      // into the user's folder.  That's only needed once.
      m_statusLabel->setText(tr("Copying language files ..."));
      QApplication::setOverrideCursor(Qt::WaitCursor);
      QString error;
      dataDir = OcrLanguages::gatherInUserDir(languages, &error);
      QApplication::restoreOverrideCursor();
      m_statusLabel->clear();
      if (dataDir.isEmpty()) {
        QMessageBox::warning(this, windowTitle(),
                             tr("The files of the selected languages are in different folders, and they could "
                                "not be copied into one folder, which text recognition needs.")
                                 + QStringLiteral("\n\n") + error);
        return;
      }
    }
    options.ocr = true;
    options.ocrDataDir = dataDir;
    options.ocrLanguages = languages.join('+');
    options.ocrPageLayout = m_pageLayout->currentData().toInt();
  }
  settings.setPdfOcrEnabled(m_ocrGroup->isChecked());
  if (m_languageList->count() > 0) {
    // Without any language files, keep the previous choice.
    settings.setPdfOcrLanguages(languages);
  }
  settings.setPdfOcrPageLayout(m_pageLayout->currentData().toInt());
#endif
  settings.setPdfJpegQuality(m_jpegQuality->value());
  settings.setPdfBackgroundScale(m_backgroundScale->currentData().toInt());
  settings.setPdfJbig2Enabled(m_bitonalCompression->currentData().toBool());
  settings.setPdfOpenAfterCreationEnabled(m_openAfterCreation->isChecked());

  const int pageCount = static_cast<int>(pages.size());
  m_job = std::make_unique<PdfExportJob>(std::move(pages), path, options);
  connect(m_job.get(), &PdfExportJob::progress, this, &PdfExportDialog::exportProgress, Qt::QueuedConnection);
  PdfExportJob* job = m_job.get();
  m_thread = QThread::create([job]() { job->run(); });
  connect(m_thread, &QThread::finished, this, &PdfExportDialog::exportFinished);

  m_runningFile = path;
  m_progressBar->setRange(0, pageCount);
  m_progressBar->setValue(0);
  m_statusLabel->setText(options.ocr ? tr("Recognizing the text and creating the PDF ...")
                                     : tr("Creating the PDF ..."));
  setRunning(true);
  m_thread->start();
}  // PdfExportDialog::startExport

void PdfExportDialog::exportProgress(const int pagesDone, const int pagesTotal) {
  if (!m_thread) {
    return;
  }
  m_progressBar->setRange(0, pagesTotal);
  m_progressBar->setValue(pagesDone);
}

void PdfExportDialog::exportFinished() {
  if (!m_thread) {
    return;
  }
  m_thread->wait();
  delete m_thread;
  m_thread = nullptr;
  const std::unique_ptr<PdfExportJob> job = std::move(m_job);
  setRunning(false);

  const QString nativePath = QDir::toNativeSeparators(m_runningFile);
  if (job->succeeded()) {
    const QString size = QLocale().formattedDataSize(QFileInfo(m_runningFile).size());
    m_statusLabel->setText(tr("The PDF was created: %1 (%2)").arg(nativePath, size));
    // Creating it again shouldn't ask about replacing our own file.
    m_overwriteConfirmed = m_runningFile;
    if (m_openAfterCreation->isChecked()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(m_runningFile));
    }
  } else if (job->wasCancelled()) {
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Cancelled.  No PDF was written."));
  } else {
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("The PDF could not be created."));
    QStringList errors = job->errors();
    const int maxShown = 20;
    if (errors.size() > maxShown) {
      const int hidden = static_cast<int>(errors.size()) - maxShown;
      errors = errors.mid(0, maxShown);
      errors.push_back(tr("... and %1 more.").arg(hidden));
    }
    QMessageBox::warning(this, windowTitle(),
                         tr("The PDF could not be created.") + QStringLiteral("\n\n") + errors.join('\n'));
  }
}  // PdfExportDialog::exportFinished

void PdfExportDialog::updateControls() {
  const bool running = (m_thread != nullptr);
  int available = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    if (m_pageList->item(row)->flags().testFlag(Qt::ItemIsUserCheckable)) {
      ++available;
    }
  }
  const int checked = checkedCount();
  m_selectionLabel->setText(tr("%1 of %2 output pages selected").arg(checked).arg(available));
  const int missing = m_pageList->count() - available;
  if (missing > 0) {
    m_selectionLabel->setText(m_selectionLabel->text() + QStringLiteral("  ")
                              + tr("(%1 pages not output yet)").arg(missing));
  }

  const bool hasSelection = !m_pageList->selectedItems().isEmpty();
  m_upButton->setEnabled(!running && hasSelection);
  m_downButton->setEnabled(!running && hasSelection);
  m_createButton->setEnabled(!running && (checked > 0));
}

void PdfExportDialog::requestThumbnail(const int entryIndex) {
  if (!m_thumbnailCache) {
    return;
  }
  auto handler = std::make_shared<ThumbnailHandler>(this, entryIndex);
  QPixmap pixmap;
  const ThumbnailPixmapCache::Status status
      = m_thumbnailCache->loadRequest(ImageId(m_entries[entryIndex].page.mainFile()), pixmap, handler);
  if (status == ThumbnailPixmapCache::LOADED) {
    if (QListWidgetItem* item = itemForEntry(entryIndex)) {
      item->setIcon(makeIcon(pixmap));
    }
    m_thumbnailHandlers[entryIndex].reset();
  } else if (status == ThumbnailPixmapCache::QUEUED) {
    // The cache only keeps a weak reference.
    m_thumbnailHandlers[entryIndex] = handler;
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

void PdfExportDialog::thumbnailLoaded(const int entryIndex, const ThumbnailLoadResult& result) {
  if (result.status() == ThumbnailLoadResult::LOADED) {
    if (QListWidgetItem* item = itemForEntry(entryIndex)) {
      item->setIcon(makeIcon(result.pixmap()));
    }
    m_thumbnailHandlers[entryIndex].reset();
  } else if (result.status() == ThumbnailLoadResult::REQUEST_EXPIRED) {
    // The cache serves the newest requests first and drops old ones.  Ask again.
    QTimer::singleShot(0, this, [this, entryIndex]() { requestThumbnail(entryIndex); });
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

QListWidgetItem* PdfExportDialog::itemForEntry(const int entryIndex) const {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->data(Qt::UserRole).toInt() == entryIndex) {
      return item;
    }
  }
  return nullptr;
}

QIcon PdfExportDialog::makeIcon(const QPixmap& thumbnail) const {
  // All icons have the same size, so that the texts line up.
  const QSize size = m_pageList->iconSize();
  QPixmap canvas(size);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  if (thumbnail.isNull()) {
    const QRect rect(QPoint(size.width() / 6, 0), QSize(size.width() * 2 / 3, size.height() - 1));
    painter.fillRect(rect, QColor(220, 220, 220));
    painter.setPen(QColor(160, 160, 160));
    painter.drawRect(rect);
  } else {
    const QPixmap scaled = thumbnail.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPoint topLeft((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2);
    painter.drawPixmap(topLeft, scaled);
    painter.setPen(QColor(160, 160, 160));
    painter.drawRect(QRect(topLeft, scaled.size()).adjusted(0, 0, -1, -1));
  }
  painter.end();
  return QIcon(canvas);
}

QString PdfExportDialog::kindText(const PdfExportPage& page) const {
  switch (page.kind()) {
    case PdfExportPage::MISSING:
      return tr("Not output yet");
    case PdfExportPage::BITONAL:
      return tr("Black and white (lossless)");
    case PdfExportPage::MRC:
      return tr("Split output (picture JPEG, text lossless)");
    case PdfExportPage::IMAGE:
      return tr("JPEG image");
  }
  return QString();
}

void PdfExportDialog::fillLanguageList(const QStringList& checked) {
  if (!m_languageList) {
    return;
  }
  // Languages sorted by name, then the script models.
  std::vector<std::tuple<bool, QString, QString>> languages;  // Is script, display name, code.
  for (const auto& entry : OcrLanguages::available()) {
    languages.emplace_back(OcrLanguages::isScript(entry.first), OcrLanguages::displayName(entry.first), entry.first);
  }
  std::sort(languages.begin(), languages.end(), [](const auto& a, const auto& b) {
    if (std::get<0>(a) != std::get<0>(b)) {
      return !std::get<0>(a);
    }
    return QString::localeAwareCompare(std::get<1>(a), std::get<1>(b)) < 0;
  });

  m_languageList->clear();
  for (const auto& language : languages) {
    auto* item = new QListWidgetItem(std::get<1>(language), m_languageList);
    item->setData(Qt::UserRole, std::get<2>(language));
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(checked.contains(std::get<2>(language)) ? Qt::Checked : Qt::Unchecked);
  }
  m_noLanguagesLabel->setVisible(languages.empty());
}

void PdfExportDialog::downloadLanguages() {
  TessdataDownloadDialog dialog(this);
  dialog.exec();
  if (!dialog.downloadedCodes().isEmpty()) {
    // New languages are ticked right away; they were downloaded to be used.
    QStringList checked = checkedLanguages();
    checked.append(dialog.downloadedCodes());
    fillLanguageList(checked);
  }
}

QStringList PdfExportDialog::checkedLanguages() const {
  QStringList codes;
  if (!m_languageList) {
    return codes;
  }
  for (int row = 0; row < m_languageList->count(); ++row) {
    const QListWidgetItem* item = m_languageList->item(row);
    if (item->checkState() == Qt::Checked) {
      codes.push_back(item->data(Qt::UserRole).toString());
    }
  }
  return codes;
}

int PdfExportDialog::checkedCount() const {
  int count = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      ++count;
    }
  }
  return count;
}

void PdfExportDialog::setRunning(const bool running) {
  m_pageList->setEnabled(!running);
  m_allButton->setEnabled(!running);
  m_noneButton->setEnabled(!running);
  m_jpegQuality->setEnabled(!running);
  m_backgroundScale->setEnabled(!running);
  m_bitonalCompression->setEnabled(!running);
  m_fileEdit->setEnabled(!running);
  m_browseButton->setEnabled(!running);
  m_openAfterCreation->setEnabled(!running);
  if (m_ocrGroup) {
    m_ocrGroup->setEnabled(!running);
  }
  m_closeButton->setEnabled(true);
  m_closeButton->setText(running ? tr("Cancel") : tr("Close"));
  updateControls();
}
