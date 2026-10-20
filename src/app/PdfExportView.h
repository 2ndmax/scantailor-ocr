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
class QFormLayout;
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
 * the options panel (optionsWidget()) into the options area on the left. Only already existing
 * output files are used. The page selection and order are kept as long as the view exists (while
 * the project is open), but they aren't stored; the options are application settings, saved as
 * soon as they are changed.
 */
class PdfExportView : public QWidget {
  Q_OBJECT
 public:
  struct Entry {
    /** Shown in the page list, e.g. "page_1L.tif". */
    QString label;
    PdfExportPage page;
  };

  /**
   * \param thumbnailCache The cache that holds the thumbnails of the output files.
   */
  explicit PdfExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache, QWidget* parent = nullptr);

  ~PdfExportView() override;

  /** The options panel. It's owned by this view. */
  QWidget* optionsWidget() const;

  /**
   * Sets the pages of the project, in project order. They are analyzed here. Pages that were
   * shown before keep their place and their tick (see PdfPageOrder).
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
  void moveForward();

  void moveBack();

  /** Numbers the ticked pages in their order in the PDF. */
  void updatePositions();

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
  class PageTileDelegate;

  QWidget* createOptionsWidget();

  /** Ticks or unticks all pages that are output. */
  void setAllChecked(Qt::CheckState state);

  void requestThumbnail(int generation, int entryIndex);

  void thumbnailLoaded(int generation, int entryIndex, const ThumbnailLoadResult& result);

  QListWidgetItem* itemForEntry(int entryIndex) const;

  /** Shows a loaded thumbnail; the first one also sets the proportions of the placeholders. */
  void showThumbnail(int entryIndex, const QPixmap& thumbnail);

  /**
   * A thumbnail, or a placeholder in the proportions of the pages if \p thumbnail is null.
   * The placeholder of a page that isn't output yet says so.
   */
  QIcon makeIcon(const QPixmap& thumbnail, bool missing = false) const;

  /** The name of the method chosen in a compression box, as shown there. */
  QString pdfMethodName(const QComboBox* box) const;

  /** The compression box and quality field that apply to the picture of a page. */
  const QComboBox* pictureCompressionBox(const PdfExportPage& page) const;

  const QSpinBox* pictureQualityBox(const PdfExportPage& page) const;

  /** Whether the PDF compresses the picture of a page with loss. */
  bool isPdfPictureLossy(const PdfExportPage& page) const;

  /** Whether a posterized picture is converted to full color by JPEG or JPEG 2000. */
  bool isPosterizedAsJpeg(const PdfExportPage& page) const;

  /** What to warn about for a page: problems with its files and losses that may be unintended. */
  QStringList pageWarnings(const PdfExportPage& page) const;

  /** What is good to know about a page without being a problem. */
  QStringList pageNotes(const PdfExportPage& page) const;

  /** Color mode, compression in the output folder and in the PDF, \p warnings and \p notes of a page. */
  QString pageToolTip(const Entry& entry, const QStringList& warnings, const QStringList& notes) const;

  /** Sets the tooltips and the warning signs of the pages for the compression currently chosen. */
  void updatePageInfos();

  /** Shows the compressions of the ticked pages in the Quality panel. */
  void updateQualityPanel();

  /** Greys out the quality fields that the chosen compression doesn't use, and updates the pages. */
  void compressionChanged();

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
  QPushButton* m_forwardButton = nullptr;
  QPushButton* m_backButton = nullptr;
  /** Set while setPages() fills the list, so that the positions are only updated once. */
  bool m_filling = false;
  QLineEdit* m_fileEdit = nullptr;
  QPushButton* m_browseButton = nullptr;
  QProgressBar* m_progressBar = nullptr;
  QLabel* m_statusLabel = nullptr;
  QPushButton* m_createButton = nullptr;
  QPushButton* m_cancelButton = nullptr;

  std::unique_ptr<QWidget> m_optionsWidget;
  QComboBox* m_colorCompression = nullptr;
  QSpinBox* m_colorQuality = nullptr;
  QComboBox* m_backgroundScale = nullptr;
  QComboBox* m_paletteCompression = nullptr;
  QSpinBox* m_paletteQuality = nullptr;
  QComboBox* m_bitonalCompression = nullptr;
  /** The Quality panel: a row for each kind of picture (color, posterized, black and white), for the TIFF and the PDF.
   */
  QFormLayout* m_qualityLayout = nullptr;
  QLabel* m_tiffQuality[3] = {};
  QLabel* m_pdfQuality[3] = {};
  QLabel* m_tiffNoPages = nullptr;
  QLabel* m_pdfNoPages = nullptr;
  QLabel* m_tiffWarning = nullptr;
  QLabel* m_pdfWarning = nullptr;
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
