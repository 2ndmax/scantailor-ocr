// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_MAINWINDOW_H_
#define SCANTAILOR_APP_MAINWINDOW_H_

#include <QMainWindow>
#include <QObjectCleanupHandler>
#include <QPointer>
#include <QSizeF>
#include <QString>
#include <QTimer>
#include <functional>
#include <memory>
#include <set>
#include <vector>

#include "AbstractCommand.h"
#include "AutoImportPanel.h"
#include "BackgroundTask.h"
#include "BeforeOrAfter.h"
#include "FilterResult.h"
#include "FilterUiInterface.h"
#include "ImageFileInfo.h"
#include "NonCopyable.h"
#include "OutputFileNameGenerator.h"
#include "PageId.h"
#include "PageRange.h"
#include "PageView.h"
#include "PdfExportView.h"
#include "ScanInsertion.h"
#include "SelectedPage.h"
#include "StatusBarPanel.h"
#include "ThumbnailSequence.h"
#include "ui_MainWindow.h"

class AbstractFilter;
class AbstractRelinker;
class ThumbnailPixmapCache;
class ProjectPages;
class PageSequence;
class StageSequence;
class PageOrderProvider;
class PageSelectionAccessor;
class FilterOptionsWidget;
class ProcessingIndicationWidget;
class ImageInfo;
class ImageViewBase;
class PageInfo;
class QStackedLayout;
class WorkerThreadPool;
class ProjectReader;
class DebugImages;
class ContentBoxPropagator;
class PageOrientationPropagator;
class ProjectCreationContext;
class ProjectOpeningContext;
class CompositeCacheDrivenTask;
class TabbedDebugImages;
class ProcessingTaskQueue;
class FixDpiDialog;
class OutOfMemoryDialog;
class ScanFolderWatcher;
class QLineF;
class QRectF;
class QLayout;

class MainWindow : public QMainWindow, private FilterUiInterface, private Ui::MainWindow {
  DECLARE_NON_COPYABLE(MainWindow)

  Q_OBJECT
 public:
  MainWindow();

  ~MainWindow() override;

  PageSequence allPages() const;

  std::set<PageId> selectedPages() const;

  std::vector<PageRange> selectedRanges() const;

 protected:
  bool eventFilter(QObject* obj, QEvent* ev) override;

  void closeEvent(QCloseEvent* event) override;

  void timerEvent(QTimerEvent* event) override;

  void changeEvent(QEvent* event) override;

 public slots:

  void openProject(const QString& projectFile);

 private:
  enum MainAreaAction { UPDATE_MAIN_AREA, CLEAR_MAIN_AREA };

 private slots:

  void autoSaveProject();

  void goFirstPage();

  void goLastPage();

  void goNextPage();

  void goPrevPage();

  void goNextSelectedPage();

  void goPrevSelectedPage();

  void execGotoPageDialog();

  void goToPage(const PageId& pageId,
                ThumbnailSequence::SelectionAction selectionAction = ThumbnailSequence::RESET_SELECTION);

  void currentPageChanged(const PageInfo& pageInfo, const QRectF& thumbRect, ThumbnailSequence::SelectionFlags flags);

  void pageContextMenuRequested(const PageInfo& pageInfo, const QPoint& screenPos, bool selected);

  void pastLastPageContextMenuRequested(const QPoint& screenPos);

  void toggleTwoPageSpreadReadingOrder();

  void thumbViewFocusToggled(bool checked);

  void thumbViewScrolled();

  void filterSelectionChanged(const QItemSelection& selected);

  void switchFilter1();

  void switchFilter2();

  void switchFilter3();

  void switchFilter4();

  void switchFilter5();

  void switchFilter6();

  void pageOrderingChanged(int idx);

  void reloadRequested();

  void startBatchProcessing();

  void stopBatchProcessing(MainAreaAction mainArea = UPDATE_MAIN_AREA);

  void invalidateThumbnail(const PageId& pageId) override;

  void invalidateThumbnail(const PageInfo& pageInfo);

  void invalidateAllThumbnails() override;

  void showRelinkingDialog();

  void filterResult(const BackgroundTaskPtr& task, const FilterResultPtr& result);

  void debugToggled(bool enabled);

  void fixDpiDialogRequested();

  void fixedDpiSubmitted();

  void sourceDpiChanged();

  void pdfExportRunningChanged(bool running);

  void saveProjectTriggered();

  void saveProjectAsTriggered();

  void newProject();

  void newProjectCreated(ProjectCreationContext* context);

  void openProject();

  void projectOpened(ProjectOpeningContext* context);

  void closeProject();

  void openSettingsDialog();

  void openDefaultParamsDialog();

  void onSettingsChanged();

  void showAboutDialog();

  void handleOutOfMemorySituation();

  void reloadCurrentPage();

  void autoImportToggled(bool importing);

  void changeAutoImportDirectory();

  void scanReady(const ImageFileInfo& file);

  void scanFailed(const QString& filePath);

 private:
  class PageSelectionProviderImpl;

  enum SavePromptResult { SAVE, DONT_SAVE, CANCEL };

  using FilterPtr = std::shared_ptr<AbstractFilter>;

  static void removeWidgetsFromLayout(QLayout* layout);

  struct SavedMainAreaViewState {
    double zoom = 1.0;
    bool hasScrollNorm = false;
    double scrollNormX = 0.5;
    double scrollNormY = 0.5;
  };

  static ImageViewBase* findPrimaryImageView(QWidget* root);

  static void applySavedMainAreaViewState(ImageViewBase* view, const SavedMainAreaViewState& state);

  void scheduleSavedMainAreaViewStateRestore(const QPointer<ImageViewBase>& view);

  void setOptionsWidget(FilterOptionsWidget* widget, Ownership ownership) override;

  void setImageWidget(QWidget* widget,
                      Ownership ownership,
                      DebugImages* debugImages = nullptr,
                      bool overlay = false) override;

  std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester() override;

  void switchToNewProject(const std::shared_ptr<ProjectPages>& pages,
                          const QString& outDir,
                          const QString& projectFilePath = QString(),
                          const ProjectReader* projectReader = nullptr,
                          const QString& inputDir = QString());

  /**
   * Whether a project with a missing output folder is still where it was: the parent of the
   * output folder and all images exist.  For a project without images, \p inputDir must exist.
   */
  static bool isProjectInPlace(const ProjectPages& pages, const QString& outDir, const QString& inputDir);

  /**
   * Makes \p projectFile, just saved, the file of the open project and the most recent project.
   */
  void setSavedProjectFile(const QString& projectFile);

  void updateThumbViewMinWidth();

  void setupThumbView();

  void showNewOpenProjectPanel();

  SavePromptResult promptProjectSave();

  static bool compareFiles(const QString& fpath1, const QString& fpath2);

  std::shared_ptr<const PageOrderProvider> currentPageOrderProvider() const;

  void updateSortOptions();

  void resetThumbSequence(const std::shared_ptr<const PageOrderProvider>& pageOrderProvider,
                          ThumbnailSequence::SelectionAction selectionAction = ThumbnailSequence::RESET_SELECTION);

  void removeFilterOptionsWidget();

  void removeImageWidget();

  void updateProjectActions();

  bool isBatchProcessingInProgress() const;

  bool isProjectLoaded() const;

  bool isBelowSelectContent() const;

  bool isBelowSelectContent(int filterIdx) const;

  bool isBelowFixOrientation(int filterIdx) const;

  bool isOutputFilter() const;

  bool isOutputFilter(int filterIdx) const;

  PageView getCurrentView() const;

  void updateMainArea();

  /** Shows the "Create PDF" step instead of the current filter. */
  void enterPdfStage();

  /** Removes the "Create PDF" step from the main area and the options area. */
  void leavePdfStage();

  /** The pages of the project in project order, for the "Create PDF" step. */
  std::vector<PdfExportView::Entry> pdfExportEntries() const;

  /** Next to the project file and named like it. */
  QString defaultPdfFile() const;

  /** The page navigation shortcuts act on the hidden thumbnails in the "Create PDF" step. */
  void setPageNavigationEnabled(bool enabled);

  bool checkReadyForOutput(const PageId* ignore = nullptr) const;

  void loadPageInteractive(const PageInfo& page);

  void updateWindowTitle();

  bool closeProjectInteractive();

  void closeProjectWithoutSaving();

  bool saveProjectWithFeedback(const QString& projectFile);

  void showInsertFileDialog(BeforeOrAfter beforeOrAfter, const ImageId& existing);

  void showRemovePagesDialog(const std::set<PageId>& pages);

  /** Returns the pages inserted, in order. */
  std::vector<PageInfo> insertImage(const ImageInfo& newImage, BeforeOrAfter beforeOrAfter, ImageId existing);

  /** Inserts all images of \p file (more than one for a multi-page TIFF).  Returns the pages inserted. */
  std::vector<PageInfo> insertImageFile(const ImageFileInfo& file, BeforeOrAfter beforeOrAfter, ImageId existing);

  void setupAutoImport();

  /** Shows or hides the "Automatic import" panel and brings it up to date. */
  void updateAutoImportPanel();

  /** The folder suggested for importing: the input folder of an empty project, else the one with the most images. */
  QString suggestedImportDirectory() const;

  /**
   * Starts importing new scans from \p dir.  If there are images in it that are not in the
   * project, the user chooses which of them to add.  Returns false if cancelled or impossible.
   */
  bool startAutoImport(const QString& dir, bool chooseExistingImages);

  void stopAutoImport();

  /** Adds a new scan where the import mode says and saves the project. */
  void importScan(const ImageFileInfo& file);

  /** The user chose another mode in the "Automatic import" panel. */
  void importModeChanged(AutoImportPanel::Mode mode);

  /** Back to adding new scans at the end. */
  void resetImportMode();

  bool isImageInProject(const QString& filePath) const;

  /**
   * The images of the selected pages, in page order, each once; a new scan replaces them.
   * \p contiguous tells whether they are next to each other.
   */
  std::vector<ImageId> imagesToReplace(bool* contiguous) const;

  /** Shows in the panel how many pages a new scan replaces, and whether it can. */
  void updateReplaceSelection();

  /**
   * Renames the new scan \p file to \p newName in its folder, retrying a few times in case the
   * scanning program still has it open.  Returns the renamed file, or \p file if that fails.
   */
  ImageFileInfo renameScan(const ImageFileInfo& file, const QString& newName);

  /** Moves an image replaced by a new scan into the folder "replaced" next to it. */
  void moveReplacedImage(const QString& filePath);

  /** Shows a warning about importing scans that doesn't block further imports. */
  void showImportWarning(const QString& text);

  /** Imports the scans that arrived while the project couldn't be changed. */
  void importDeferredScans();

  /** Whether scans can be added right now, i.e. no batch processing and no PDF is being created. */
  bool canImportScansNow() const;

  void removeFromProject(const std::set<PageId>& pages);

  void eraseOutputFiles(const std::set<PageId>& pages);

  BackgroundTaskPtr createCompositeTask(const PageInfo& page, int lastFilterIdx, bool batch, bool debug);

  std::shared_ptr<CompositeCacheDrivenTask> createCompositeCacheDrivenTask(int lastFilterIdx);

  void createBatchProcessingWidget();

  void updateDisambiguationRecords(const PageSequence& pages);

  void performRelinking(const std::shared_ptr<AbstractRelinker>& relinker);

  PageSelectionAccessor newPageSelectionAccessor();

  void setDockWidgetsVisible(bool state);

  void scaleThumbnails(int scaleFactor);

  void updateMaxLogicalThumbSize();

  void updateThumbnailViewMode();

  void updateAutoSaveTimer();

  PageSequence currentPageSequence();

  void setupIcons();

  QSizeF m_maxLogicalThumbSize;
  int m_deskewHandleDistance = 0;  // As last applied to the image views, in percent.
  std::shared_ptr<ProjectPages> m_pages;
  std::shared_ptr<StageSequence> m_stages;
  QString m_projectFile;
  // The input directory of a project without images, written into the project file.
  QString m_emptyProjectInputDir;
  OutputFileNameGenerator m_outFileNameGen;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::unique_ptr<ThumbnailSequence> m_thumbSequence;
  std::unique_ptr<WorkerThreadPool> m_workerThreadPool;
  std::unique_ptr<ProcessingTaskQueue> m_batchQueue;
  std::unique_ptr<ProcessingTaskQueue> m_interactiveQueue;
  QStackedLayout* m_imageFrameLayout;
  QStackedLayout* m_optionsFrameLayout;
  QPointer<FilterOptionsWidget> m_optionsWidget;
  QPointer<FixDpiDialog> m_fixDpiDialog;
  std::unique_ptr<TabbedDebugImages> m_tabbedDebugImages;
  std::unique_ptr<ContentBoxPropagator> m_contentBoxPropagator;
  std::unique_ptr<PageOrientationPropagator> m_pageOrientationPropagator;
  std::unique_ptr<QWidget> m_batchProcessingWidget;
  std::unique_ptr<ProcessingIndicationWidget> m_processingIndicationWidget;
  /** The "Create PDF" step; created when it's first shown, kept while the project is open. */
  std::unique_ptr<PdfExportView> m_pdfView;
  /** Whether the "Create PDF" step is shown.  m_curFilter then still refers to the last filter. */
  bool m_pdfStage = false;
  /** The "Automatic import" panel above the options of the first step; owned by the options area. */
  AutoImportPanel* m_autoImportPanel = nullptr;
  std::unique_ptr<ScanFolderWatcher> m_scanWatcher;
  /** The folder shown in the panel, watched while importing is on. */
  QString m_importDir;
  /** Scans that arrived during batch processing or while a PDF was being created. */
  std::vector<ImageFileInfo> m_deferredScans;
  int m_scansWithoutDpi = 0;
  AutoImportPanel::Mode m_importMode = AutoImportPanel::APPEND;
  ScanInsertionAnchor m_insertAnchor;
  /** While non-zero, page selections don't move the anchor, e.g. while removing pages. */
  int m_ignoreAnchorSelection = 0;
  std::function<bool()> m_checkBeepWhenFinished;
  SelectedPage m_selectedPage;
  QObjectCleanupHandler m_optionsWidgetCleanup;
  QObjectCleanupHandler m_imageWidgetCleanup;
  std::unique_ptr<OutOfMemoryDialog> m_outOfMemoryDialog;
  int m_curFilter;
  SavedMainAreaViewState m_savedMainAreaViewState;
  int m_ignoreSelectionChanges;
  int m_ignorePageOrderingChanges;
  bool m_debug;
  bool m_closing;
  QTimer m_autoSaveTimer;
  StatusBarPanel* m_statusBarPanel;
  QActionGroup* m_unitsMenuActionGroup;
  QTimer m_maxLogicalThumbSizeUpdater;
  QTimer m_sceneItemsPosUpdater;
};


#endif  // ifndef SCANTAILOR_APP_MAINWINDOW_H_
