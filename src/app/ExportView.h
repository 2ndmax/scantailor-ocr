// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_EXPORTVIEW_H_
#define SCANTAILOR_APP_EXPORTVIEW_H_

#include <QString>
#include <QStringList>
#include <QWidget>
#include <memory>
#include <set>
#include <vector>

#include "ExportJob.h"
#include "ExportPage.h"
#include "ThumbnailPixmapCache.h"

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
class OcrPanel;
class PdfCompressionPanel;
class ExportFormatsPanel;
class ThumbnailLoadResult;
struct ProcessingOptions;

/**
 * \brief The Export step: picks and orders the output files of the project and writes them in the
 * chosen formats (PDF, TXT, hOCR, ALTO).
 *
 * The view itself (page list, target folder and name, progress) goes into the main area of the main window,
 * the options panel (optionsWidget()) into the options area on the left. Only already existing
 * output files are used. The page selection and order are kept as long as the view exists (while
 * the project is open), but they aren't stored; the options are application settings, saved as
 * soon as they are changed.
 */
class ExportView : public QWidget {
  Q_OBJECT
 public:
  struct Entry {
    /** Shown in the page list, e.g. "page_1L.tif". */
    QString label;
    ExportPage page;
  };

  /**
   * \param thumbnailCache The cache that holds the thumbnails of the output files.
   * \param processingOptions The settings of the project, which hold those of the PDF.
   */
  ExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
             std::shared_ptr<ProcessingOptions> processingOptions,
             QWidget* parent = nullptr);

  ~ExportView() override;

  /** The options panel. It's owned by this view. */
  QWidget* optionsWidget() const;

  /**
   * Sets the pages of the project, in project order, already analyzed (see
   * StageSequence::pdfExportPages()). Pages that were shown before keep their place
   * and their tick (see PdfPageOrder).
   * Ignored while an export runs.
   */
  void setPages(std::vector<Entry> entries);

  /**
   * Sets the suggested folder and name, given as one path without extension (see
   * project_files::defaultExportBase()), unless the user has chosen them.
   */
  void setDefaultBase(const QString& base);

  bool isRunning() const;

  /**
   * The pages whose output is missing or outdated, as the page list shows them. The files aren't
   * inspected again.
   */
  std::set<PageId> pagesToOutput() const;

  /** Cancels a running export and waits until it has stopped. */
  void cancelAndWait();

 signals:
  /** Emitted when an export starts and when it has finished. */
  void runningChanged(bool running);

  /** The export options of the project (formats, compression, text recognition) were changed. */
  void projectOptionsChanged();

  /** The user wants the pages that aren't output yet to be processed in the Output step. */
  void outputMissingPagesRequested();

 private slots:
  void moveForward();

  void moveBack();

  /** Numbers the ticked pages in their order in the export. */
  void updatePositions();

  void browse();

  void startExport();

  void cancelExport();

  void exportProgress(int pagesDone, int pagesTotal);

  void exportFinished();

  void updateControls();

  /** Stores the options in the project. */
  void saveSettings();

 private:
  class ThumbnailHandler;
  class PageTileDelegate;

  QWidget* createOptionsWidget();

  /** Greys out the panels that the chosen formats don't use. */
  void updateFormatDependents();

  /**
   * The folder and name of the exported files as one path without extension, see
   * ExportPaths::fromBase(). Empty if the fields don't give a usable one; \p problem then says why.
   */
  QString exportBase(QString* problem) const;

  /** The chosen outputs that exist already, as they are named in the question about replacing them. */
  QStringList existingOutputs(const ExportPaths& paths, const ProcessingOptions::Formats& formats) const;

  /** The status after a successful export, with links that open the files and the folder. */
  void showCreatedFiles();

  /**
   * Shows a message in place of the preview of the files. It stands until the folder, the name, an
   * option or the ticked pages change; then the preview comes back (showPreview()).
   */
  void showStatus(const QString& text, Qt::TextFormat format = Qt::PlainText);

  /** Back to the preview of the files the export will create. */
  void showPreview();

  /** Sets the preview. */
  void showPreviewText(const QString& text, Qt::TextFormat format = Qt::PlainText);

  /** A heading with the items below it, one per line with a bullet. All are rich text already. */
  static QString bulletList(const QString& heading, const QStringList& items);

  /** The ticked pages in their order, as indexes of m_entries. */
  std::vector<int> checkedEntries() const;

  /** Ticks or unticks all pages that are output. */
  void setAllChecked(Qt::CheckState state);

  void requestThumbnail(int generation, int entryIndex);

  void thumbnailLoaded(int generation, int entryIndex, const ThumbnailLoadResult& result);

  QListWidgetItem* itemForEntry(int entryIndex) const;

  /** Shows a loaded thumbnail; the first one also sets the proportions of the placeholders. */
  void showThumbnail(int entryIndex, const QPixmap& thumbnail);

  /**
   * A thumbnail, or a placeholder in the proportions of the pages if \p thumbnail is null.
   * The placeholder of a page that isn't output yet says so and has the hourglass. A page changed
   * since the output has a circular arrow.
   */
  QIcon makeIcon(const QPixmap& thumbnail, bool missing = false, bool outdated = false) const;

  /** The name of the method chosen in a compression box, as shown there. */
  QString pdfMethodName(const QComboBox* box) const;

  /** The compression box and quality field that apply to the picture of a page. */
  const QComboBox* pictureCompressionBox(const ExportPage& page) const;

  const QSpinBox* pictureQualityBox(const ExportPage& page) const;

  /** How a page loses quality with the compression currently chosen. */
  struct PageQuality {
    /** The compression that applies to the picture: 0 color / grayscale, 1 posterized, 2 black and white. */
    int part = 0;
    /** Its files have a problem, see ExportPage::warning(). */
    bool fileProblem = false;
    /** The output folder stores the picture with lossy compression. */
    bool tiffPictureLossy = false;
    /** Split output: the output folder stores the text with lossy compression. */
    bool tiffTextLossy = false;
    /** The PDF compresses the picture with loss. */
    bool pdfLossy = false;
    /** JPEG or JPEG 2000 converts a posterized picture to full color. */
    bool posterizedAsJpeg = false;
    /** A mixed page without split output files. */
    bool withoutSplitOutput = false;

    /** Whether pageWarnings() has something to say. */
    bool hasWarnings() const {
      return fileProblem || tiffPictureLossy || tiffTextLossy || posterizedAsJpeg || (withoutSplitOutput && pdfLossy);
    }

    /** Whether pageNotes() has something to say. */
    bool hasNotes() const { return withoutSplitOutput && !pdfLossy; }
  };

  PageQuality pageQuality(const ExportPage& page) const;

  /** What to warn about for a page: problems with its files and losses that may be unintended. */
  QStringList pageWarnings(const ExportPage& page, const PageQuality& quality) const;

  /** What is good to know about a page without being a problem. */
  QStringList pageNotes(const PageQuality& quality) const;

  /**
   * The tooltip of a tile outside the file name: color mode, compression in the output folder and
   * in the PDF, warnings and notes. Built when it's shown, for the compression chosen then.
   */
  QString pageToolTip(int entryIndex) const;

  /** Sets the warning signs of the pages for the compression currently chosen. */
  void updatePageSigns();

  /** Shows the compressions of the ticked pages in the Quality panel. */
  void updateQualityPanel();

  /** Greys out the quality fields that the chosen compression doesn't use, and updates the pages. */
  void compressionChanged();

  int checkedCount() const;

  void setRunning(bool running);

  std::vector<Entry> m_entries;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::shared_ptr<ProcessingOptions> m_processingOptions;
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
  QPushButton* m_outputMissingButton = nullptr;
  QPushButton* m_forwardButton = nullptr;
  QPushButton* m_backButton = nullptr;
  /** Set while setPages() fills the list, so that the positions are only updated once. */
  bool m_filling = false;
  QLineEdit* m_folderEdit = nullptr;
  QLineEdit* m_nameEdit = nullptr;
  QPushButton* m_browseButton = nullptr;
  QProgressBar* m_progressBar = nullptr;
  /**
   * Beside the folder and the name: which files the export will create or what is wrong with the
   * fields (the preview), or else a message, such as the result of the last export.
   */
  QLabel* m_statusLabel = nullptr;
  bool m_showingStatus = false;
  /** The ticked pages when the message was shown. */
  std::vector<int> m_statusPages;
  QPushButton* m_createButton = nullptr;
  QPushButton* m_cancelButton = nullptr;

  std::unique_ptr<QWidget> m_optionsWidget;
  ExportFormatsPanel* m_formatsPanel = nullptr;
  // Greyed out while the formats don't use them.
  QWidget* m_ocrGroup = nullptr;
  QWidget* m_compressionGroup = nullptr;
  QWidget* m_qualityGroup = nullptr;
  PdfCompressionPanel* m_compressionPanel = nullptr;
  // The fields of the compression panel, for the Quality panel.
  QComboBox* m_colorCompression = nullptr;
  QSpinBox* m_colorQuality = nullptr;
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
  /** The OCR options. */
  OcrPanel* m_ocrPanel = nullptr;

  /** Set once the user has typed or chosen the folder or the name; setDefaultBase() doesn't replace them then. */
  bool m_fileChosen = false;

  std::unique_ptr<ExportJob> m_job;
  QThread* m_thread = nullptr;
  /** Where the running export writes, and what. */
  ExportPaths m_runningPaths;
  ProcessingOptions::Formats m_runningFormats;
};


#endif  // SCANTAILOR_APP_EXPORTVIEW_H_
