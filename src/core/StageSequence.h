// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_STAGESEQUENCE_H_
#define SCANTAILOR_CORE_STAGESEQUENCE_H_

#include <QStringList>
#include <functional>
#include <memory>
#include <vector>

#include "AbstractFilter.h"
#include "BackgroundTask.h"
#include "NonCopyable.h"
#include "PageId.h"
#include "PdfExportPage.h"
#include "filters/deskew/Filter.h"
#include "filters/fix_orientation/Filter.h"
#include "filters/output/Filter.h"
#include "filters/page_layout/Filter.h"
#include "filters/page_split/Filter.h"
#include "filters/select_content/Filter.h"

class PageInfo;
class ProjectPages;
class ProjectReader;
class PageSelectionAccessor;
class AbstractRelinker;
class CompositeCacheDrivenTask;
class OutputFileNameGenerator;
class ThumbnailPixmapCache;

class StageSequence {
  DECLARE_NON_COPYABLE(StageSequence)

 public:
  using FilterPtr = std::shared_ptr<AbstractFilter>;

  /**
   * A page that processAll() couldn't process, and why.
   */
  struct PageFailure {
    enum Kind {
      /** The image file couldn't be loaded. */
      LOAD_FAILED,
      /** The image was loaded, but processing it failed with an unexpected error. */
      PROCESSING_FAILED,
      /** The output file couldn't be written. */
      OUTPUT_NOT_WRITTEN
    };

    PageId page;
    Kind kind;
    /** Why, as the step that failed reports it; empty for OUTPUT_NOT_WRITTEN. */
    QStringList reasons;
  };

  struct ProcessAllResult {
    /** The pages that failed, in the order they were processed. */
    std::vector<PageFailure> failures;
    /**
     * Whether the Output step ran through all pages. A page can still fail in it (e.g. its file
     * was removed in the meantime), so check failures as well.
     */
    bool output = false;
    /** Whether processing was cancelled; pages output before that keep their output files. */
    bool cancelled = false;
  };

  /**
   * Called by processAll() after each page with the step the pages are processed up to and how many
   * of the pages of this pass are done. Returning false cancels processing before the next page,
   * so no output file is left half-written.
   */
  using ProgressCallback = std::function<bool(int lastFilterIdx, int done, int total)>;

  StageSequence(const std::shared_ptr<ProjectPages>& pages, const PageSelectionAccessor& pageSelectionAccessor);

  virtual ~StageSequence() = default;

  void performRelinking(const AbstractRelinker& relinker);

  /**
   * Sets up a new project: its settings and start values are taken from \p startValues.
   */
  void startNewProject(const DefaultParams& startValues);

  /**
   * Loads the settings, start values and page parameters of a saved project.
   * What an older project lacks is taken from \p fallback.
   */
  void loadProject(const ProjectReader& reader, const DefaultParams& fallback);

  /**
   * Gives each step the start values for \p page if it has no parameters of its own yet.
   */
  void loadDefaultSettings(const PageInfo& page) const;

  /**
   * The task that loads \p page and processes it up to the step \p lastFilterIdx.
   */
  BackgroundTaskPtr createTask(const PageInfo& page,
                               int lastFilterIdx,
                               const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                               const OutputFileNameGenerator& outFileNameGen,
                               const std::shared_ptr<ProjectPages>& pages,
                               bool batch,
                               bool debug) const;

  /**
   * Processes all pages through all six steps in this thread, as batch processing in the app does
   * step by step. Which images have two pages is only known after the step that splits them, so
   * the images are processed up to that step first. The pages get the same size, which is only
   * known once the content of all pages is, so all pages go through the Margins step before any
   * is output. The pages are those the steps were created with.
   *
   * An image that fails to load or to process is left out of the later passes, and no page is
   * output, even if the sizes of all pages are known from an earlier run. The steps before the
   * output still store their results and update the thumbnails, also when no page is output.
   * Running out of memory ends processing with a PROCESSING_FAILED entry for that page.
   *
   * \param thumbnailCache Where the thumbnails are updated; may be null if none are needed.
   * \param progress Optional; see ProgressCallback.
   */
  ProcessAllResult processAll(const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                              const OutputFileNameGenerator& outFileNameGen,
                              const ProgressCallback& progress = {}) const;

  /**
   * The task that updates the thumbnails up to the step \p lastFilterIdx from the stored results.
   */
  std::shared_ptr<CompositeCacheDrivenTask> createCacheDrivenTask(int lastFilterIdx,
                                                                  const OutputFileNameGenerator& outFileNameGen) const;

  /**
   * The output files of the pages for the PDF export, in the order of the project.
   * They are already analyzed (see PdfExportPage::analyze()), so they can be exported as they are,
   * and pages whose output no longer matches their settings are marked (PdfExportPage::isOutdated()).
   */
  std::vector<PdfExportPage> pdfExportPages(const ProjectPages& pages,
                                            const OutputFileNameGenerator& outFileNameGen) const;

  /**
   * The settings of the project that apply to all its pages, shared with the steps that use them.
   * Changes apply to the tasks created afterwards.
   */
  const std::shared_ptr<ProcessingOptions>& processingOptions() const { return m_processingOptions; }

  /**
   * The start values of the project: a copy of the default parameters, which pages without
   * parameters of their own start with. Its processing options aren't used; those of the project
   * are processingOptions().
   */
  const std::shared_ptr<DefaultParams>& defaultParams() const { return m_defaultParams; }

  const std::vector<FilterPtr>& filters() const { return m_filters; }

  int count() const { return static_cast<int>(m_filters.size()); }

  const FilterPtr& filterAt(int idx) const { return m_filters[idx]; }

  int findFilter(const FilterPtr& filter) const;

  const std::shared_ptr<fix_orientation::Filter>& fixOrientationFilter() const { return m_fixOrientationFilter; }

  const std::shared_ptr<page_split::Filter>& pageSplitFilter() const { return m_pageSplitFilter; }

  const std::shared_ptr<deskew::Filter>& deskewFilter() const { return m_deskewFilter; }

  const std::shared_ptr<select_content::Filter>& selectContentFilter() const { return m_selectContentFilter; }

  const std::shared_ptr<page_layout::Filter>& pageLayoutFilter() const { return m_pageLayoutFilter; }

  const std::shared_ptr<output::Filter>& outputFilter() const { return m_outputFilter; }

  int fixOrientationFilterIdx() const { return m_fixOrientationFilterIdx; }

  int pageSplitFilterIdx() const { return m_pageSplitFilterIdx; }

  int deskewFilterIdx() const { return m_deskewFilterIdx; }

  int selectContentFilterIdx() const { return m_selectContentFilterIdx; }

  int pageLayoutFilterIdx() const { return m_pageLayoutFilterIdx; }

  int outputFilterIdx() const { return m_outputFilterIdx; }

 private:
  std::shared_ptr<ProjectPages> m_pages;
  // Before the filters, which get it when they are created.
  std::shared_ptr<ProcessingOptions> m_processingOptions;
  std::shared_ptr<DefaultParams> m_defaultParams;
  std::shared_ptr<fix_orientation::Filter> m_fixOrientationFilter;
  std::shared_ptr<page_split::Filter> m_pageSplitFilter;
  std::shared_ptr<deskew::Filter> m_deskewFilter;
  std::shared_ptr<select_content::Filter> m_selectContentFilter;
  std::shared_ptr<page_layout::Filter> m_pageLayoutFilter;
  std::shared_ptr<output::Filter> m_outputFilter;
  std::vector<FilterPtr> m_filters;
  int m_fixOrientationFilterIdx;
  int m_pageSplitFilterIdx;
  int m_deskewFilterIdx;
  int m_selectContentFilterIdx;
  int m_pageLayoutFilterIdx;
  int m_outputFilterIdx;
};


#endif  // ifndef SCANTAILOR_CORE_STAGESEQUENCE_H_
