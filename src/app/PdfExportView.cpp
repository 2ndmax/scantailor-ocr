// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportView.h"

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
#include "CollapsibleGroupBox.h"
#include "ImageId.h"
#include "OcrEngine.h"
#include "OcrLanguages.h"
#include "PdfExportJob.h"
#include "TessdataDownloadDialog.h"
#include "ThumbnailLoadResult.h"

namespace {
/** The language list grows with the number of languages up to this many rows, then it scrolls. */
const int MAX_VISIBLE_LANGUAGES = 8;
}  // namespace

class PdfExportView::ThumbnailHandler : public ThumbnailPixmapCache::CompletionHandler {
 public:
  ThumbnailHandler(PdfExportView* view, const int generation, const int entryIndex)
      : m_view(view), m_generation(generation), m_entryIndex(entryIndex) {}

  void operator()(const ThumbnailLoadResult& result) override {
    m_view->thumbnailLoaded(m_generation, m_entryIndex, result);
  }

 private:
  PdfExportView* m_view;
  int m_generation;
  int m_entryIndex;
};


PdfExportView::PdfExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache, QWidget* parent)
    : QWidget(parent), m_thumbnailCache(std::move(thumbnailCache)) {
  auto* mainLayout = new QVBoxLayout(this);

  // Page list.
  auto* pagesGroup = new QGroupBox(tr("Pages"));
  auto* pagesLayout = new QVBoxLayout(pagesGroup);
  m_pageList = new QListWidget;
  m_pageList->setIconSize(QSize(64, 90));
  m_pageList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_pageList->setDragDropMode(QAbstractItemView::InternalMove);
  m_pageList->setDefaultDropAction(Qt::MoveAction);
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
  mainLayout->addWidget(pagesGroup, 1);

  // Target file.
  auto* fileLayout = new QHBoxLayout;
  fileLayout->addWidget(new QLabel(tr("PDF file:")));
  m_fileEdit = new QLineEdit;
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

  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  m_cancelButton = new QPushButton(tr("Cancel"));
  m_cancelButton->setVisible(false);
  m_createButton = new QPushButton(tr("Create PDF"));
  m_createButton->setDefault(true);
  buttons->addWidget(m_cancelButton);
  buttons->addWidget(m_createButton);
  mainLayout->addLayout(buttons);

  m_optionsWidget.reset(createOptionsWidget());

  connect(m_pageList, &QListWidget::itemChanged, this, &PdfExportView::updateControls);
  connect(m_pageList, &QListWidget::itemSelectionChanged, this, &PdfExportView::updateControls);
  connect(m_allButton, &QPushButton::clicked, this, &PdfExportView::selectAll);
  connect(m_noneButton, &QPushButton::clicked, this, &PdfExportView::selectNone);
  connect(m_upButton, &QPushButton::clicked, this, &PdfExportView::moveUp);
  connect(m_downButton, &QPushButton::clicked, this, &PdfExportView::moveDown);
  connect(m_fileEdit, &QLineEdit::textEdited, this, [this]() { m_fileChosen = true; });
  connect(m_browseButton, &QPushButton::clicked, this, &PdfExportView::browse);
  connect(m_createButton, &QPushButton::clicked, this, &PdfExportView::startExport);
  connect(m_cancelButton, &QPushButton::clicked, this, &PdfExportView::cancelExport);

  updateControls();
}

PdfExportView::~PdfExportView() {
  if (m_thread) {
    m_job->cancel();
    m_thread->wait();
    delete m_thread;
  }
}

QWidget* PdfExportView::createOptionsWidget() {
  const ApplicationSettings& settings = ApplicationSettings::getInstance();

  auto* widget = new QWidget;
  auto* layout = new QVBoxLayout(widget);
  layout->setContentsMargins(0, 0, 0, 0);

  // Collapsible like the panels of the other steps; the object name keeps its collapsed state.
  auto* compressionGroup = new CollapsibleGroupBox(tr("Compression"));
  compressionGroup->setObjectName("pdfCompressionPanel");
  auto* optionsLayout = new QFormLayout(compressionGroup);
  // The options panel is narrow: put the fields below their labels if needed.
  optionsLayout->setRowWrapPolicy(QFormLayout::WrapLongRows);

  m_jpegQuality = new QSpinBox;
  m_jpegQuality->setRange(10, 100);
  m_jpegQuality->setValue(settings.getPdfJpegQuality());
  m_jpegQuality->setToolTip(
      tr("Higher values give better pictures and larger files.  Applies to grayscale and color pages and to the "
         "pictures of pages with split output.  All pages that are neither black and white nor have split output "
         "are stored as JPEG."));
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

  layout->addWidget(compressionGroup);

  connect(m_jpegQuality, qOverload<int>(&QSpinBox::valueChanged), this, &PdfExportView::saveSettings);
  connect(m_backgroundScale, qOverload<int>(&QComboBox::currentIndexChanged), this, &PdfExportView::saveSettings);
  connect(m_bitonalCompression, qOverload<int>(&QComboBox::currentIndexChanged), this, &PdfExportView::saveSettings);

#ifdef ENABLE_OCR
  // Text recognition.  Like the panels of the other steps, the check box that turns it on
  // is the first entry, not part of the title.
  auto* ocrGroup = new CollapsibleGroupBox(tr("Text recognition (OCR)"));
  ocrGroup->setObjectName("pdfOcrPanel");
  auto* ocrGroupLayout = new QVBoxLayout(ocrGroup);
  m_ocrEnabled = new QCheckBox(tr("Recognize text"));
  m_ocrEnabled->setChecked(settings.isPdfOcrEnabled());
  m_ocrEnabled->setToolTip(
      tr("Adds an invisible text layer, so the text of the PDF can be searched, selected and copied.  Text "
         "recognition takes a few seconds per page."));
  ocrGroupLayout->addWidget(m_ocrEnabled);

  // Greyed out while text recognition is off.
  m_ocrOptions = new QWidget;
  auto* ocrLayout = new QVBoxLayout(m_ocrOptions);
  ocrLayout->setContentsMargins(0, 0, 0, 0);
  ocrGroupLayout->addWidget(m_ocrOptions);
  ocrLayout->addWidget(new QLabel(tr("Languages:")));
  m_languageList = new QListWidget;
  m_languageList->setSelectionMode(QAbstractItemView::NoSelection);
  m_languageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_languageList->setToolTip(tr("Tick the languages of the text.  Several languages can be ticked."));
  ocrLayout->addWidget(m_languageList);

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
  connect(moreLanguagesButton, &QPushButton::clicked, this, &PdfExportView::downloadLanguages);

  fillLanguageList(settings.getPdfOcrLanguages());

  auto* layoutRow = new QFormLayout;
  layoutRow->setRowWrapPolicy(QFormLayout::WrapLongRows);
  m_pageLayout = new QComboBox;
  m_pageLayout->addItem(tr("Automatic"), static_cast<int>(OcrEngine::AUTOMATIC_LAYOUT));
  m_pageLayout->addItem(tr("Single column"), static_cast<int>(OcrEngine::SINGLE_COLUMN));
  m_pageLayout->addItem(tr("Single block of text"), static_cast<int>(OcrEngine::SINGLE_BLOCK));
  m_pageLayout->setCurrentIndex(std::max(0, m_pageLayout->findData(settings.getPdfOcrPageLayout())));
  const QString textLayoutTip = tr(
      "How the text is arranged on the pages.  This decides the order in which the text is recognized, "
      "e.g. for searching and copying; the look of the PDF doesn't change.  \"Automatic\" detects columns, "
      "pictures and captions and is usually right.  \"Single column\" helps if copied text comes out in the "
      "wrong order, e.g. with indented lines or tables.  \"Single block of text\" suits pages with only one "
      "short text, such as a label or a note.");
  m_pageLayout->setToolTip(textLayoutTip);
  auto* textLayoutLabel = new QLabel(tr("Text layout:"));
  textLayoutLabel->setToolTip(textLayoutTip);
  layoutRow->addRow(textLayoutLabel, m_pageLayout);
  ocrLayout->addLayout(layoutRow);

  m_ocrOptions->setEnabled(m_ocrEnabled->isChecked());
  layout->addWidget(ocrGroup);

  connect(m_ocrEnabled, &QCheckBox::toggled, m_ocrOptions, &QWidget::setEnabled);
  connect(m_ocrEnabled, &QCheckBox::toggled, this, &PdfExportView::saveSettings);
  connect(m_languageList, &QListWidget::itemChanged, this, &PdfExportView::saveSettings);
  connect(m_pageLayout, qOverload<int>(&QComboBox::currentIndexChanged), this, &PdfExportView::saveSettings);
#endif

  // Not about the content of the PDF, so outside the groups.
  m_openAfterCreation = new QCheckBox(tr("Open the PDF after creating it"));
  m_openAfterCreation->setChecked(settings.isPdfOpenAfterCreationEnabled());
  layout->addWidget(m_openAfterCreation);
  connect(m_openAfterCreation, &QCheckBox::toggled, this, &PdfExportView::saveSettings);

  layout->addStretch(1);
  return widget;
}  // PdfExportView::createOptionsWidget

QWidget* PdfExportView::optionsWidget() const {
  return m_optionsWidget.get();
}

void PdfExportView::setPages(std::vector<Entry> entries) {
  if (m_thread) {
    return;
  }

  // Thumbnails still being loaded belong to the previous pages.
  ++m_generation;
  m_entries = std::move(entries);
  m_thumbnailHandlers.clear();
  m_thumbnailHandlers.resize(m_entries.size());
  m_thumbnailShown.assign(m_entries.size(), false);

  // Only the file headers are read, so this is quick even for many pages.
  QApplication::setOverrideCursor(Qt::WaitCursor);
  for (Entry& entry : m_entries) {
    entry.page.analyze();
  }
  QApplication::restoreOverrideCursor();

  {
    const QSignalBlocker blocker(m_pageList);
    m_pageList->clear();
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
      }
    }
  }
  for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
    if (m_entries[i].page.kind() != PdfExportPage::MISSING) {
      requestThumbnail(m_generation, i);
    }
  }
  updateControls();
}  // PdfExportView::setPages

void PdfExportView::setDefaultFile(const QString& file) {
  if (!m_fileChosen) {
    m_fileEdit->setText(QDir::toNativeSeparators(file));
  }
}

bool PdfExportView::isRunning() const {
  return m_thread != nullptr;
}

void PdfExportView::cancelAndWait() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  m_thread->wait();
  exportFinished();
}

void PdfExportView::selectAll() {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      item->setCheckState(Qt::Checked);
    }
  }
}

void PdfExportView::selectNone() {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      item->setCheckState(Qt::Unchecked);
    }
  }
}

void PdfExportView::moveUp() {
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

void PdfExportView::moveDown() {
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

void PdfExportView::browse() {
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
  m_fileChosen = true;
}

void PdfExportView::startExport() {
  if (m_thread) {
    return;
  }
  const QString title = tr("Create PDF");

  QString path = QDir::fromNativeSeparators(m_fileEdit->text().trimmed());
  if (path.isEmpty()) {
    QMessageBox::warning(this, title, tr("Please enter the file name of the PDF."));
    return;
  }
  if (!path.endsWith(".pdf", Qt::CaseInsensitive)) {
    path += ".pdf";
  }
  const QFileInfo fileInfo(path);
  if (fileInfo.isRelative()) {
    QMessageBox::warning(this, title, tr("Please enter the complete path of the PDF, including the folder."));
    return;
  }
  path = QDir::cleanPath(fileInfo.absoluteFilePath());
  m_fileEdit->setText(QDir::toNativeSeparators(path));
  if (!fileInfo.absoluteDir().exists()) {
    QMessageBox::warning(this, title,
                         tr("The folder %1 doesn't exist.").arg(QDir::toNativeSeparators(fileInfo.absolutePath())));
    return;
  }
  if (fileInfo.isDir()) {
    QMessageBox::warning(this, title, tr("%1 is a folder.").arg(QDir::toNativeSeparators(path)));
    return;
  }
  if (fileInfo.exists() && (path != m_overwriteConfirmed)) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, title, tr("The file %1 already exists.\nDo you want to replace it?").arg(QDir::toNativeSeparators(path)),
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
    QMessageBox::information(this, title, tr("Please tick the pages to include in the PDF."));
    return;
  }

  PdfExportOptions options;
  options.jpegQuality = m_jpegQuality->value();
  options.backgroundScale = m_backgroundScale->currentData().toInt();
  options.jbig2 = m_bitonalCompression->currentData().toBool();
  options.threadCount = PdfExportJob::defaultThreadCount();

#ifdef ENABLE_OCR
  if (m_ocrEnabled->isChecked()) {
    const QStringList languages = checkedLanguages();
    if (languages.isEmpty()) {
      QMessageBox::warning(this, title,
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
        QMessageBox::warning(this, title,
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
#endif

  const int pageCount = static_cast<int>(pages.size());
  m_job = std::make_unique<PdfExportJob>(std::move(pages), path, options);
  connect(m_job.get(), &PdfExportJob::progress, this, &PdfExportView::exportProgress, Qt::QueuedConnection);
  PdfExportJob* job = m_job.get();
  m_thread = QThread::create([job]() { job->run(); });
  connect(m_thread, &QThread::finished, this, &PdfExportView::exportFinished);

  m_runningFile = path;
  m_progressBar->setRange(0, pageCount);
  m_progressBar->setValue(0);
  m_statusLabel->setText(options.ocr ? tr("Recognizing the text and creating the PDF ...")
                                     : tr("Creating the PDF ..."));
  setRunning(true);
  m_thread->start();
}  // PdfExportView::startExport

void PdfExportView::cancelExport() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  m_statusLabel->setText(tr("Cancelling ..."));
  m_cancelButton->setEnabled(false);
}

void PdfExportView::exportProgress(const int pagesDone, const int pagesTotal) {
  if (!m_thread) {
    return;
  }
  m_progressBar->setRange(0, pagesTotal);
  m_progressBar->setValue(pagesDone);
}

void PdfExportView::exportFinished() {
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
    QMessageBox::warning(this, tr("Create PDF"),
                         tr("The PDF could not be created.") + QStringLiteral("\n\n") + errors.join('\n'));
  }
}  // PdfExportView::exportFinished

void PdfExportView::updateControls() {
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

void PdfExportView::saveSettings() {
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  settings.setPdfJpegQuality(m_jpegQuality->value());
  settings.setPdfBackgroundScale(m_backgroundScale->currentData().toInt());
  settings.setPdfJbig2Enabled(m_bitonalCompression->currentData().toBool());
  settings.setPdfOpenAfterCreationEnabled(m_openAfterCreation->isChecked());
#ifdef ENABLE_OCR
  settings.setPdfOcrEnabled(m_ocrEnabled->isChecked());
  if (m_languageList->count() > 0) {
    // Without any language files, keep the previous choice.
    settings.setPdfOcrLanguages(checkedLanguages());
  }
  settings.setPdfOcrPageLayout(m_pageLayout->currentData().toInt());
#endif
}

void PdfExportView::requestThumbnail(const int generation, const int entryIndex) {
  if (!m_thumbnailCache || (generation != m_generation) || (entryIndex >= static_cast<int>(m_entries.size()))) {
    return;
  }
  auto handler = std::make_shared<ThumbnailHandler>(this, generation, entryIndex);
  QPixmap pixmap;
  const ThumbnailPixmapCache::Status status
      = m_thumbnailCache->loadRequest(ImageId(m_entries[entryIndex].page.mainFile()), pixmap, handler);
  if (status == ThumbnailPixmapCache::LOADED) {
    showThumbnail(entryIndex, pixmap);
    m_thumbnailHandlers[entryIndex].reset();
  } else if (status == ThumbnailPixmapCache::QUEUED) {
    // The cache only keeps a weak reference.
    m_thumbnailHandlers[entryIndex] = handler;
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

void PdfExportView::thumbnailLoaded(const int generation, const int entryIndex, const ThumbnailLoadResult& result) {
  if (generation != m_generation) {
    return;
  }
  if (result.status() == ThumbnailLoadResult::LOADED) {
    showThumbnail(entryIndex, result.pixmap());
    m_thumbnailHandlers[entryIndex].reset();
  } else if (result.status() == ThumbnailLoadResult::REQUEST_EXPIRED) {
    // The cache serves the newest requests first and drops old ones.  Ask again.
    QTimer::singleShot(0, this, [this, generation, entryIndex]() { requestThumbnail(generation, entryIndex); });
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

QListWidgetItem* PdfExportView::itemForEntry(const int entryIndex) const {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->data(Qt::UserRole).toInt() == entryIndex) {
      return item;
    }
  }
  return nullptr;
}

void PdfExportView::showThumbnail(const int entryIndex, const QPixmap& thumbnail) {
  if (QListWidgetItem* item = itemForEntry(entryIndex)) {
    item->setIcon(makeIcon(thumbnail));
  }
  m_thumbnailShown[entryIndex] = true;

  if (!m_placeholderAspectKnown && !thumbnail.isNull() && (thumbnail.height() > 0)) {
    // The pages of a project usually have the same proportions; draw the placeholders like them.
    m_placeholderAspectKnown = true;
    m_placeholderAspect = double(thumbnail.width()) / thumbnail.height();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
      if (!m_thumbnailShown[i]) {
        if (QListWidgetItem* item = itemForEntry(i)) {
          item->setIcon(makeIcon(QPixmap()));
        }
      }
    }
  }
}

QIcon PdfExportView::makeIcon(const QPixmap& thumbnail) const {
  // All icons have the same size, so that the texts line up.
  const QSize size = m_pageList->iconSize();
  QPixmap canvas(size);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  if (thumbnail.isNull()) {
    // Centered like the thumbnails, which keep their proportions.
    QSize placeholderSize = QSize(qRound(size.height() * m_placeholderAspect), size.height());
    if (placeholderSize.width() > size.width()) {
      placeholderSize = QSize(size.width(), qRound(size.width() / m_placeholderAspect));
    }
    placeholderSize = placeholderSize.expandedTo(QSize(2, 2));
    const QPoint topLeft((size.width() - placeholderSize.width()) / 2, (size.height() - placeholderSize.height()) / 2);
    const QRect rect = QRect(topLeft, placeholderSize).adjusted(0, 0, -1, -1);
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

QString PdfExportView::kindText(const PdfExportPage& page) const {
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

void PdfExportView::fillLanguageList(const QStringList& checked) {
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

  {
    // Filling the list isn't a change by the user.
    const QSignalBlocker blocker(m_languageList);
    m_languageList->clear();
    for (const auto& language : languages) {
      auto* item = new QListWidgetItem(std::get<1>(language), m_languageList);
      item->setData(Qt::UserRole, std::get<2>(language));
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
      item->setCheckState(checked.contains(std::get<2>(language)) ? Qt::Checked : Qt::Unchecked);
    }
  }
  m_noLanguagesLabel->setVisible(languages.empty());
  updateLanguageListHeight();
}

void PdfExportView::updateLanguageListHeight() {
  const int count = m_languageList->count();
  m_languageList->setVisible(count > 0);
  if (count == 0) {
    return;
  }
  int rowHeight = m_languageList->sizeHintForRow(0);
  if (rowHeight <= 0) {
    rowHeight = m_languageList->fontMetrics().height() + 4;
  }
  const int rows = std::min(count, MAX_VISIBLE_LANGUAGES);
  m_languageList->setFixedHeight(rows * rowHeight + 2 * m_languageList->frameWidth());
}

void PdfExportView::downloadLanguages() {
  TessdataDownloadDialog dialog(this);
  dialog.exec();
  if (!dialog.downloadedCodes().isEmpty()) {
    // New languages are ticked right away; they were downloaded to be used.
    QStringList checked = checkedLanguages();
    checked.append(dialog.downloadedCodes());
    fillLanguageList(checked);
    saveSettings();
  }
}

QStringList PdfExportView::checkedLanguages() const {
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

int PdfExportView::checkedCount() const {
  int count = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      ++count;
    }
  }
  return count;
}

void PdfExportView::setRunning(const bool running) {
  m_pageList->setEnabled(!running);
  m_allButton->setEnabled(!running);
  m_noneButton->setEnabled(!running);
  m_fileEdit->setEnabled(!running);
  m_browseButton->setEnabled(!running);
  m_optionsWidget->setEnabled(!running);
  m_cancelButton->setVisible(running);
  m_cancelButton->setEnabled(running);
  updateControls();
  emit runningChanged(running);
}
