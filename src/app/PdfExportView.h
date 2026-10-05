// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PDFEXPORTVIEW_H_
#define SCANTAILOR_APP_PDFEXPORTVIEW_H_

#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <vector>

#include "PdfExportPage.h"
#include "ThumbnailPixmapCache.h"

class QCheckBox;
class QComboBox;
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
 * \brief The "Create PDF" step: picks, orders and combines the output files of the project into a PDF.
 *
 * The view itself (page list, target file, progress) goes into the main area of the main window,
 * the options panel (optionsWidget()) into the options area on the left.  Only already existing
 * output files are used.  The page selection and order aren't stored; the options are application
 * settings, saved as soon as they are changed.
 */
class PdfExportView : public QWidget {
  Q_OBJECT
 public:
  struct Entry {
    /** Shown in the page list, e.g. "3 - page_1L.tif". */
    QString label;
    PdfExportPage page;
  };

  /**
   * \param thumbnailCache The cache that holds the thumbnails of the output files.
   */
  explicit PdfExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache, QWidget* parent = nullptr);

  ~PdfExportView() override;

  /** The options panel.  It's owned by this view. */
  QWidget* optionsWidget() const;

  /**
   * Sets the pages of the project, in project order.  They are analyzed here.
   * Ignored while a PDF is being created.
   */
  void setPages(std::vector<Entry> entries);

  /** Sets the suggested PDF file, unless the user has chosen one. */
  void setDefaultFile(const QString& file);

  bool isRunning() const;

  /** Cancels a running export and waits until it has stopped. */
  void cancelAndWait();

 signals:
  /** Emitted when creating a PDF starts and when it has finished. */
  void runningChanged(bool running);

 private slots:
  void selectAll();

  void selectNone();

  void moveUp();

  void moveDown();

  void browse();

  void startExport();

  void cancelExport();

  void exportProgress(int pagesDone, int pagesTotal);

  void exportFinished();

  void updateControls();

  /** Stores the options as application settings. */
  void saveSettings();

  /** Opens the dialog for downloading OCR languages. */
  void downloadLanguages();

 private:
  class ThumbnailHandler;

  QWidget* createOptionsWidget();

  void requestThumbnail(int generation, int entryIndex);

  void thumbnailLoaded(int generation, int entryIndex, const ThumbnailLoadResult& result);

  QListWidgetItem* itemForEntry(int entryIndex) const;

  /** Shows a loaded thumbnail; the first one also sets the proportions of the placeholders. */
  void showThumbnail(int entryIndex, const QPixmap& thumbnail);

  /** A thumbnail, or a placeholder in the proportions of the pages if \p thumbnail is null. */
  QIcon makeIcon(const QPixmap& thumbnail) const;

  QString kindText(const PdfExportPage& page) const;

  int checkedCount() const;

  /** The codes of the ticked OCR languages. */
  QStringList checkedLanguages() const;

  /** Lists the installed OCR languages and ticks those in \p checked. */
  void fillLanguageList(const QStringList& checked);

  /** Shows up to 8 languages; more can be scrolled. */
  void updateLanguageListHeight();

  void setRunning(bool running);

  std::vector<Entry> m_entries;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::vector<std::shared_ptr<ThumbnailPixmapCache::CompletionHandler>> m_thumbnailHandlers;
  /** Incremented by setPages(), so that thumbnails of earlier pages are ignored. */
  int m_generation = 0;
  /** Which entries show their thumbnail rather than a placeholder. */
  std::vector<bool> m_thumbnailShown;
  /** Width / height of the placeholders: A4 portrait until a thumbnail has been loaded. */
  double m_placeholderAspect = 0.7071;
  bool m_placeholderAspectKnown = false;

  QListWidget* m_pageList = nullptr;
  QLabel* m_selectionLabel = nullptr;
  QPushButton* m_allButton = nullptr;
  QPushButton* m_noneButton = nullptr;
  QPushButton* m_upButton = nullptr;
  QPushButton* m_downButton = nullptr;
  QLineEdit* m_fileEdit = nullptr;
  QPushButton* m_browseButton = nullptr;
  QProgressBar* m_progressBar = nullptr;
  QLabel* m_statusLabel = nullptr;
  QPushButton* m_createButton = nullptr;
  QPushButton* m_cancelButton = nullptr;

  std::unique_ptr<QWidget> m_optionsWidget;
  QSpinBox* m_jpegQuality = nullptr;
  QComboBox* m_backgroundScale = nullptr;
  QComboBox* m_bitonalCompression = nullptr;
  QCheckBox* m_openAfterCreation = nullptr;
  /** The OCR options; these stay null in builds without OCR. */
  QCheckBox* m_ocrEnabled = nullptr;
  /** Languages and text layout, greyed out while text recognition is off. */
  QWidget* m_ocrOptions = nullptr;
  QListWidget* m_languageList = nullptr;
  QLabel* m_noLanguagesLabel = nullptr;
  QComboBox* m_pageLayout = nullptr;

  /** Set once the user has typed or chosen a file; setDefaultFile() doesn't replace it then. */
  bool m_fileChosen = false;
  /** The file the user already confirmed to overwrite in the file dialog. */
  QString m_overwriteConfirmed;

  std::unique_ptr<PdfExportJob> m_job;
  QThread* m_thread = nullptr;
  QString m_runningFile;
};


#endif  // SCANTAILOR_APP_PDFEXPORTVIEW_H_
