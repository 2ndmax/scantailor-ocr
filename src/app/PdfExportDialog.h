// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PDFEXPORTDIALOG_H_
#define SCANTAILOR_APP_PDFEXPORTDIALOG_H_

#include <QDialog>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "PdfExportPage.h"
#include "ThumbnailPixmapCache.h"

class QCheckBox;
class QComboBox;
class QGroupBox;
class QIcon;
class QLabel;
class QPixmap;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QThread;
class PdfExportJob;
class ThumbnailLoadResult;

/**
 * \brief Lets the user pick, order and combine the output files of the project into a PDF.
 *
 * Only already existing output files are used.  The page selection and order
 * aren't stored; the options are application settings.
 */
class PdfExportDialog : public QDialog {
  Q_OBJECT
 public:
  struct Entry {
    /** Shown in the page list, e.g. "3 - page_1L.tif". */
    QString label;
    PdfExportPage page;
  };

  /**
   * \param entries The pages of the project in project order.  They are analyzed by the dialog.
   * \param thumbnailCache The cache that holds the thumbnails of the output files.
   * \param defaultFile The suggested PDF file.
   */
  PdfExportDialog(std::vector<Entry> entries,
                  std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                  const QString& defaultFile,
                  QWidget* parent = nullptr);

  ~PdfExportDialog() override;

 public slots:
  void reject() override;

 private slots:
  void selectAll();

  void selectNone();

  void moveUp();

  void moveDown();

  void browse();

  void startExport();

  void exportProgress(int pagesDone, int pagesTotal);

  void exportFinished();

  void updateControls();

  /** Opens the dialog for downloading OCR languages. */
  void downloadLanguages();

 private:
  class ThumbnailHandler;

  void requestThumbnail(int entryIndex);

  void thumbnailLoaded(int entryIndex, const ThumbnailLoadResult& result);

  QListWidgetItem* itemForEntry(int entryIndex) const;

  QIcon makeIcon(const QPixmap& thumbnail) const;

  QString kindText(const PdfExportPage& page) const;

  int checkedCount() const;

  /** The codes of the ticked OCR languages. */
  QStringList checkedLanguages() const;

  /** Lists the installed OCR languages and ticks those in \p checked. */
  void fillLanguageList(const QStringList& checked);

  void setRunning(bool running);

  std::vector<Entry> m_entries;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::vector<std::shared_ptr<ThumbnailPixmapCache::CompletionHandler>> m_thumbnailHandlers;

  QListWidget* m_pageList = nullptr;
  QLabel* m_selectionLabel = nullptr;
  QPushButton* m_allButton = nullptr;
  QPushButton* m_noneButton = nullptr;
  QPushButton* m_upButton = nullptr;
  QPushButton* m_downButton = nullptr;
  QSpinBox* m_jpegQuality = nullptr;
  QComboBox* m_backgroundScale = nullptr;
  QComboBox* m_bitonalCompression = nullptr;
  QCheckBox* m_openAfterCreation = nullptr;
  /** The OCR options; these stay null in builds without OCR. */
  QGroupBox* m_ocrGroup = nullptr;
  QListWidget* m_languageList = nullptr;
  QLabel* m_noLanguagesLabel = nullptr;
  QComboBox* m_pageLayout = nullptr;
  QLineEdit* m_fileEdit = nullptr;
  QPushButton* m_browseButton = nullptr;
  QProgressBar* m_progressBar = nullptr;
  QLabel* m_statusLabel = nullptr;
  QPushButton* m_createButton = nullptr;
  QPushButton* m_closeButton = nullptr;

  /** The file the user already confirmed to overwrite in the file dialog. */
  QString m_overwriteConfirmed;

  std::unique_ptr<PdfExportJob> m_job;
  QThread* m_thread = nullptr;
  QString m_runningFile;
};


#endif  // SCANTAILOR_APP_PDFEXPORTDIALOG_H_
