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
#include <PageInfo.h>
#include <StageSequence.h>
#include <ThumbnailCollector.h>

#include <QGraphicsItem>
#include <QSizeF>
#include <QWidget>
#include <memory>
#include <vector>

#include "test_project.h"

/**
 * Showing a page as the main window shows it, with a stand-in for the window, so the tests can
 * check what each step puts on screen: its options panel, its image view, how the two are
 * connected, and its thumbnails.
 */
namespace filter_ui_checks {
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

// Keeps the thumbnail a step creates for a page.
class RecordingThumbnails : public ThumbnailCollector {
 public:
  explicit RecordingThumbnails(std::shared_ptr<ThumbnailPixmapCache> cache) : m_cache(std::move(cache)) {}

  void processThumbnail(std::unique_ptr<QGraphicsItem> item) override { thumbnail = std::move(item); }

  std::shared_ptr<ThumbnailPixmapCache> thumbnailCache() override { return m_cache; }

  QSizeF maxLogicalThumbSize() const override { return QSizeF(200, 200); }

  std::unique_ptr<QGraphicsItem> thumbnail;

 private:
  std::shared_ptr<ThumbnailPixmapCache> m_cache;
};

/**
 * Shows \p page in the step \p filterIdx as the main window does: the options panel first, then
 * the result of processing the page up to that step. Returns the result, null if there is none.
 */
inline FilterResultPtr showPage(test_pages::TestProject& project,
                                const PageInfo& page,
                                const int filterIdx,
                                RecordingUi& ui) {
  project.stages->loadDefaultSettings(page);
  project.stages->filterAt(filterIdx)->preUpdateUI(&ui, page);
  const FilterResultPtr result = (*project.stages->createTask(page, filterIdx, project.thumbnailCache,
                                                              project.outFileNameGen, project.pages, false, false))();
  if (result) {
    result->updateUI(&ui);
  }
  return result;
}

// The thumbnail of \p page in the step \p filterIdx, as the thumbnail list gets it.
inline std::unique_ptr<QGraphicsItem> thumbnailFor(test_pages::TestProject& project,
                                                   const PageInfo& page,
                                                   const int filterIdx) {
  RecordingThumbnails thumbnails(project.thumbnailCache);
  project.stages->createCacheDrivenTask(filterIdx, project.outFileNameGen)->process(page, &thumbnails);
  return std::move(thumbnails.thumbnail);
}
}  // namespace filter_ui_checks

#endif  // SCANTAILOR_QT_TESTS_FILTER_UI_CHECKS_H_
