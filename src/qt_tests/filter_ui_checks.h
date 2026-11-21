// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_QT_TESTS_FILTER_UI_CHECKS_H_
#define SCANTAILOR_QT_TESTS_FILTER_UI_CHECKS_H_

#include <AbstractFilter.h>
#include <BackgroundTask.h>
#include <CompositeCacheDrivenTask.h>
#include <FilterOptionsWidget.h>
#include <FilterResult.h>
#include <FilterUiInterface.h>
#include <FilterUiSequence.h>
#include <PageInfo.h>
#include <PageRange.h>
#include <PageSelectionAccessor.h>
#include <PageSelectionProvider.h>
#include <PageSequence.h>
#include <StageSequence.h>
#include <ThumbnailCollector.h>
#include <ThumbnailDataBase.h>

#include <QGraphicsItem>
#include <QSizeF>
#include <QTemporaryDir>
#include <QWidget>
#include <memory>
#include <set>
#include <vector>

#include "test_project.h"

/**
 * Showing a page as the main window shows it, with a stand-in for the window, so the tests can
 * check what each step puts on screen: its options panel, its image view, how the two are
 * connected, and its thumbnails.
 */
namespace filter_ui_checks {
// A stand-in for the main window's page selection. Unlike a null accessor, it can be asked for pages
// by an apply dialog's PageScopeGroupBox.
class EmptyPageSelectionProvider : public PageSelectionProvider {
 public:
  PageSequence allPages() const override { return {}; }
  std::set<PageId> selectedPages() const override { return {}; }
  std::vector<PageRange> selectedRanges() const override { return {}; }
};

// Records what a step shows, in place of the main window.
class RecordingUi : public FilterUiInterface {
 public:
  void setOptionsWidget(FilterOptionsWidget* widget, const Ownership ownership) override {
    optionsWidget = widget;
    optionsWidgetOwnership = ownership;
  }

  void setImageWidget(QWidget* widget,
                      const Ownership ownership,
                      DebugImages* /*debugImages*/,
                      bool /*overlay*/) override {
    if (ownership == TRANSFER_OWNERSHIP) {
      imageWidget.reset(widget);
    }
  }

  void invalidateThumbnail(const PageId& pageId) override { invalidatedThumbnails.push_back(pageId); }

  void invalidateAllThumbnails() override { ++allThumbnailsInvalidated; }

  std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester() override { return nullptr; }

  FilterOptionsWidget* optionsWidget = nullptr;
  Ownership optionsWidgetOwnership = TRANSFER_OWNERSHIP;
  std::unique_ptr<QWidget> imageWidget;
  std::vector<PageId> invalidatedThumbnails;
  int allThumbnailsInvalidated = 0;
};

// Keeps what a step describes as the thumbnail of a page.
class RecordingThumbnails : public ThumbnailCollector {
 public:
  void processThumbnail(std::unique_ptr<ThumbnailDataBase> data) override { thumbnail = std::move(data); }

  std::unique_ptr<ThumbnailDataBase> thumbnail;
};

// A project of one drawn page, test_pages::skewedPage(), in a temporary folder.
class OnePageProject {
 public:
  explicit OnePageProject(const DefaultParams& startValues = DefaultParams())
      : m_files({test_pages::saveScan(test_pages::skewedPage(), m_dir.filePath("page.png"))}),
        project(m_files, m_dir.filePath("out"), startValues),
        filterUis(project.stages, PageSelectionAccessor(nullptr)) {}

  // Whether the page could be saved, which needs the PNG plugin.
  bool isValid() const { return m_dir.isValid() && test_pages::allSaved(m_files); }

  // The page as the steps up to the split see it (IMAGE_VIEW) or as the later ones do (PAGE_VIEW).
  PageInfo page(const PageView view) const { return project.pages->toPageSequence(view).pageAt(0); }

 private:
  // Before the project, which is set up from them.
  QTemporaryDir m_dir;
  std::vector<ImageFileInfo> m_files;

 public:
  test_pages::TestProject project;
  // What the main window shows of the steps of the project.
  FilterUiSequence filterUis;
};

/**
 * Shows \p page in the step \p filterIdx as the main window does: the options panel first, then
 * the result of processing the page up to that step. Returns the result, null if there is none.
 */
inline FilterResultPtr showPage(OnePageProject& one, const PageInfo& page, const int filterIdx, RecordingUi& ui) {
  test_pages::TestProject& project = one.project;
  project.stages->loadDefaultSettings(page);
  one.filterUis.preUpdateUI(filterIdx, &ui, page);
  const FilterResultPtr result = (*project.stages->createTask(page, filterIdx, project.thumbnailCache,
                                                              project.outFileNameGen, project.pages, false, false))();
  if (result) {
    result->updateUI(&ui);
    one.filterUis.showResult(*result, &ui);
  }
  return result;
}

// The thumbnail of \p page in the step \p filterIdx, as the thumbnail list gets it.
inline std::unique_ptr<QGraphicsItem> thumbnailFor(OnePageProject& one, const PageInfo& page, const int filterIdx) {
  RecordingThumbnails thumbnails;
  one.project.stages->createCacheDrivenTask(filterIdx, one.project.outFileNameGen)->process(page, &thumbnails);
  if (!thumbnails.thumbnail) {
    return nullptr;
  }
  return one.filterUis.createThumbnail(*thumbnails.thumbnail, one.project.thumbnailCache, QSizeF(200, 200));
}
}  // namespace filter_ui_checks

#endif  // SCANTAILOR_QT_TESTS_FILTER_UI_CHECKS_H_
