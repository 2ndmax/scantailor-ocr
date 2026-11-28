// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StageSequence.h"
#include "DefaultParams.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <exception>
#include <map>
#include <new>
#include <optional>
#include <queue>
#include <set>
#include <utility>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QMutex>
#include <QRunnable>
#include <QThreadPool>
#include <QWaitCondition>

#include "LoadFileTask.h"
#include "OutputFileNameGenerator.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ThreadBudget.h"
#include "filters/deskew/CacheDrivenTask.h"
#include "filters/deskew/Task.h"
#include "filters/fix_orientation/CacheDrivenTask.h"
#include "filters/fix_orientation/Task.h"
#include "filters/output/CacheDrivenTask.h"
#include "filters/output/OutputStatusCollector.h"
#include "filters/output/Task.h"
#include "filters/output/Utils.h"
#include "filters/page_layout/CacheDrivenTask.h"
#include "filters/page_layout/Task.h"
#include "filters/page_split/CacheDrivenTask.h"
#include "filters/page_split/Task.h"
#include "filters/select_content/CacheDrivenTask.h"
#include "filters/select_content/Task.h"

StageSequence::StageSequence(const std::shared_ptr<ProjectPages>& pages)
    : m_pages(pages),
      m_processingOptions(std::make_shared<ProcessingOptions>()),
      m_defaultParams(std::make_shared<DefaultParams>()),
      m_fixOrientationFilter(std::make_shared<fix_orientation::Filter>()),
      m_pageSplitFilter(std::make_shared<page_split::Filter>(pages)),
      // Aliased shared_ptrs: the filters see the same DeskewParams instance that
      // DefaultParams holds, so in-place assignment (*m_defaultParams = ...)
      // propagates to them. Depends on DefaultParams only assigning in place.
      m_deskewFilter(std::make_shared<deskew::Filter>(
          m_processingOptions,
          std::shared_ptr<const deskew::DeskewParams>(m_defaultParams, &m_defaultParams->getDeskewParams()))),
      m_selectContentFilter(std::make_shared<select_content::Filter>(
          std::shared_ptr<const select_content::SelectContentParams>(m_defaultParams,
                                                                     &m_defaultParams->getSelectContentParams()))),
      m_pageLayoutFilter(std::make_shared<page_layout::Filter>(pages)),
      m_outputFilter(std::make_shared<output::Filter>(m_processingOptions)) {
  m_fixOrientationFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_fixOrientationFilter);

  m_pageSplitFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_pageSplitFilter);

  m_deskewFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_deskewFilter);

  m_selectContentFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_selectContentFilter);

  m_pageLayoutFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_pageLayoutFilter);

  m_outputFilterIdx = static_cast<int>(m_filters.size());
  m_filters.emplace_back(m_outputFilter);
}

void StageSequence::performRelinking(const AbstractRelinker& relinker) {
  for (FilterPtr& filter : m_filters) {
    filter->performRelinking(relinker);
  }
}

int StageSequence::findFilter(const FilterPtr& filter) const {
  int idx = 0;
  for (const FilterPtr& f : m_filters) {
    if (f == filter) {
      return idx;
    }
    ++idx;
  }
  return -1;
}

void StageSequence::startNewProject(const DefaultParams& startValues) {
  *m_processingOptions = startValues.getProcessingOptions();
  *m_defaultParams = startValues;
  m_pageLayoutFilter->loadNewProjectSettings(m_defaultParams->getPageLayoutParams(),
                                             m_defaultParams->getOutputParams().getDpi(), m_defaultParams->getUnits());
}

void StageSequence::loadProject(const ProjectReader& reader, const DefaultParams& fallback) {
  *m_processingOptions = reader.readProcessingOptions(fallback.getProcessingOptions());
  std::unique_ptr<DefaultParams> startValues = reader.readDefaultParams();
  *m_defaultParams = startValues ? *startValues : fallback;
  reader.readFilterSettings(m_filters);
}

void StageSequence::loadDefaultSettings(const PageInfo& page) const {
  m_fixOrientationFilter->loadDefaultSettings(page, m_defaultParams->getFixOrientationParams());
  m_pageSplitFilter->loadDefaultSettings(page, m_defaultParams->getPageSplitParams());
  m_deskewFilter->loadDefaultSettings(page, m_defaultParams->getDeskewParams());
  m_selectContentFilter->loadDefaultSettings(page, m_defaultParams->getSelectContentParams(),
                                             m_defaultParams->getUnits());
  m_pageLayoutFilter->loadDefaultSettings(page, m_defaultParams->getPageLayoutParams(), m_defaultParams->getUnits());
  m_outputFilter->loadDefaultSettings(page, m_defaultParams->getOutputParams());
}

BackgroundTaskPtr StageSequence::createTask(const PageInfo& page,
                                            const int lastFilterIdx,
                                            const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                                            const OutputFileNameGenerator& outFileNameGen,
                                            const std::shared_ptr<ProjectPages>& pages,
                                            const bool batch,
                                            bool debug) const {
  std::shared_ptr<fix_orientation::Task> fixOrientationTask;
  std::shared_ptr<page_split::Task> pageSplitTask;
  std::shared_ptr<deskew::Task> deskewTask;
  std::shared_ptr<select_content::Task> selectContentTask;
  std::shared_ptr<page_layout::Task> pageLayoutTask;
  std::shared_ptr<output::Task> outputTask;

  if (batch) {
    debug = false;
  }

  if (lastFilterIdx >= m_outputFilterIdx) {
    outputTask = m_outputFilter->createTask(page.id(), thumbnailCache, outFileNameGen, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_pageLayoutFilterIdx) {
    pageLayoutTask = m_pageLayoutFilter->createTask(page.id(), outputTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_selectContentFilterIdx) {
    selectContentTask = m_selectContentFilter->createTask(page.id(), pageLayoutTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_deskewFilterIdx) {
    deskewTask = m_deskewFilter->createTask(page.id(), selectContentTask, batch, debug);
    debug = false;
  }
  if (lastFilterIdx >= m_pageSplitFilterIdx) {
    pageSplitTask = m_pageSplitFilter->createTask(page, deskewTask, batch, debug);
  }
  if (lastFilterIdx >= m_fixOrientationFilterIdx) {
    fixOrientationTask = m_fixOrientationFilter->createTask(page.id(), pageSplitTask, batch);
  }
  assert(fixOrientationTask);
  return std::make_shared<LoadFileTask>(batch ? BackgroundTask::BATCH : BackgroundTask::INTERACTIVE, page,
                                        thumbnailCache, pages, fixOrientationTask);
}  // StageSequence::createTask

namespace {
StageSequence::PageFailure::Kind toPageFailureKind(const LoadFileTask::Failure failure) {
  switch (failure) {
    case LoadFileTask::Failure::LOAD:
      return StageSequence::PageFailure::LOAD_FAILED;
    case LoadFileTask::Failure::OUTPUT_TOO_LARGE:
      return StageSequence::PageFailure::OUTPUT_TOO_LARGE;
    case LoadFileTask::Failure::PROCESSING:
      break;
  }
  return StageSequence::PageFailure::PROCESSING_FAILED;
}

/** What the task of one page gave back, in a thread of the pool. */
struct PageOutcome {
  int pageIndex = 0;
  FilterResultPtr result;
  bool outOfMemory = false;
  std::exception_ptr exception;
};

/** Hands the outcomes from the threads of the pool to the thread that runs the pass. */
class OutcomeQueue {
 public:
  void push(PageOutcome outcome) {
    const QMutexLocker locker(&m_mutex);
    m_outcomes.push(std::move(outcome));
    m_ready.wakeOne();
  }

  PageOutcome waitAndPop() {
    const QMutexLocker locker(&m_mutex);
    while (m_outcomes.empty()) {
      m_ready.wait(&m_mutex);
    }
    PageOutcome outcome = std::move(m_outcomes.front());
    m_outcomes.pop();
    return outcome;
  }

 private:
  QMutex m_mutex;
  QWaitCondition m_ready;
  std::queue<PageOutcome> m_outcomes;
};

/** Runs the task of one page and always reports back, also when it was stopped or threw. */
class PageRunnable : public QRunnable {
 public:
  PageRunnable(const int pageIndex, BackgroundTaskPtr task, OutcomeQueue& outcomes, const std::atomic<bool>& stopping)
      : m_pageIndex(pageIndex), m_task(std::move(task)), m_outcomes(outcomes), m_stopping(stopping) {}

  void run() override {
    // The pages processed side by side share the threads of the image filters and decoders.
    const ThreadBudget::TaskScope taskScope;
    PageOutcome outcome;
    outcome.pageIndex = m_pageIndex;
    // A task still waiting in the pool when the pass stops doesn't start.
    if (!m_stopping) {
      try {
        outcome.result = (*m_task)();
      } catch (const std::bad_alloc&) {
        // LoadFileTask leaves it to the out-of-memory handler of the app's thread pool, which
        // isn't used here.
        // REVIEW-NOTE (2026-11): a page task runs out of memory -> the pass stops and the page fails with
        // "Out of memory.". No test reaches this path, because a test can't make a page task throw bad_alloc
        // without running the machine out of memory.
        outcome.outOfMemory = true;
      } catch (...) {
        // An exception leaving a thread of the pool would end the program; the pass throws it again.
        outcome.exception = std::current_exception();
      }
    }
    m_outcomes.push(std::move(outcome));
  }

 private:
  const int m_pageIndex;
  const BackgroundTaskPtr m_task;
  OutcomeQueue& m_outcomes;
  const std::atomic<bool>& m_stopping;
};

/**
 * The failures of one pass. Per image, only the failure of its first page counts, as when the
 * pages ran one after the other: the other page of a split image was skipped then. Run side by
 * side, both may already be running, and either may fail first.
 */
class PassFailures {
 public:
  void addImageFailure(const int pageIndex, const PageInfo& page, StageSequence::PageFailure failure) {
    const auto it = m_firstPageOfImage.find(page.imageId());
    if ((it != m_firstPageOfImage.end()) && (it->second < pageIndex)) {
      return;
    }
    if (it != m_firstPageOfImage.end()) {
      m_failures.erase(it->second);
    }
    m_firstPageOfImage[page.imageId()] = pageIndex;
    m_failures[pageIndex] = std::move(failure);
  }

  void addOtherFailure(const int pageIndex, StageSequence::PageFailure failure) {
    m_failures[pageIndex] = std::move(failure);
  }

  /** Appends the failures in the order of the pages. */
  void appendTo(std::vector<StageSequence::PageFailure>& failures) const {
    for (const auto& [pageIndex, failure] : m_failures) {
      failures.push_back(failure);
    }
  }

 private:
  std::map<int, StageSequence::PageFailure> m_failures;
  std::map<ImageId, int> m_firstPageOfImage;
};

/** Mutable state of one StageSequence::processPass() execution. */
struct PassState {
  PassState(const StageSequence& stages,
            const std::shared_ptr<ProjectPages>& projectPages,
            const int lastFilterIdx,
            const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
            const OutputFileNameGenerator& outFileNameGen,
            std::set<ImageId>& failedImages,
            const StageSequence::ProgressCallback& progress,
            std::vector<PageInfo> pages)
      : stages(stages),
        projectPages(projectPages),
        lastFilterIdx(lastFilterIdx),
        thumbnailCache(thumbnailCache),
        outFileNameGen(outFileNameGen),
        failedImages(failedImages),
        progress(progress),
        pages(std::move(pages)),
        total(static_cast<int>(this->pages.size())) {}

  const StageSequence& stages;
  const std::shared_ptr<ProjectPages>& projectPages;
  const int lastFilterIdx;
  const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache;
  const OutputFileNameGenerator& outFileNameGen;
  std::set<ImageId>& failedImages;
  const StageSequence::ProgressCallback& progress;
  std::vector<PageInfo> pages;
  const int total;
  int done = 0;
  int nextPage = 0;
  int running = 0;
  bool cancelled = false;
  int outOfMemoryPage = -1;
  std::exception_ptr exception;
  // Set by this thread only. Once set, no page starts any more and the outcomes of those still
  // running are only waited for: so no output file is left half-written.
  std::atomic<bool> stopping{false};
  OutcomeQueue outcomes;
  PassFailures failures;
};

/** Records one completed or skipped page and returns false if processing is to be cancelled. */
bool reportPageProgress(PassState& state,
                        const PageInfo& page,
                        const StageSequence::PageProgress::Status status,
                        const StageSequence::PageFailure* failure = nullptr) {
  ++state.done;
  // A cancel after the last page changes nothing: all pages are done, so the result counts as complete.
  const bool lastPage = (state.lastFilterIdx == state.stages.outputFilterIdx()) && (state.done == state.total);
  if (!state.progress) {
    return true;
  }
  StageSequence::PageProgress p;
  p.lastFilterIdx = state.lastFilterIdx;
  p.done = state.done;
  p.total = state.total;
  p.page = page;
  p.status = status;
  if (failure) {
    p.failure = *failure;
  }
  return state.progress(p) || lastPage;
}

/** Starts page tasks up to the pool's thread limit, skipping pages of already-failed images. */
void startAvailableTasks(PassState& state, QThreadPool& pool) {
  // Start pages while there are free threads. Not all at once: the default settings of a page
  // are loaded right before it starts, and a stop leaves the others untouched.
  while (!state.stopping && (state.nextPage < state.total) && (state.running < pool.maxThreadCount())) {
    const int pageIndex = state.nextPage++;
    const PageInfo& page = state.pages[pageIndex];
    if (state.failedImages.count(page.imageId()) != 0) {
      // The image failed in an earlier pass or with its other page.
      if (!reportPageProgress(state, page, StageSequence::PageProgress::SKIPPED)) {
        state.cancelled = true;
        state.stopping = true;
      }
      continue;
    }
    // The pages that only exist after the split get their start values in the second pass;
    // in the output pass all pages have their parameters.
    if (state.lastFilterIdx != state.stages.outputFilterIdx()) {
      state.stages.loadDefaultSettings(page);
    }
    pool.start(new PageRunnable(pageIndex,
                                state.stages.createTask(page, state.lastFilterIdx, state.thumbnailCache,
                                                        state.outFileNameGen, state.projectPages, true, false),
                                state.outcomes, state.stopping));
    ++state.running;
  }
}

/** Processes one completed page outcome from the worker pool and updates pass state. */
void handlePageOutcome(PassState& state, const PageOutcome& outcome) {
  if (state.stopping) {
    return;
  }
  if (outcome.exception) {
    state.exception = outcome.exception;
    state.stopping = true;
    return;
  }
  if (outcome.outOfMemory) {
    // Further pages would most likely fail the same way.
    state.outOfMemoryPage = outcome.pageIndex;
    state.stopping = true;
    return;
  }
  const PageInfo& page = state.pages[outcome.pageIndex];
  QStringList reasons;
  LoadFileTask::Failure failure = LoadFileTask::Failure::LOAD;
  std::optional<StageSequence::PageFailure> pageFailure;
  if (LoadFileTask::isFailure(outcome.result, &reasons, &failure)) {
    state.failedImages.insert(page.imageId());
    pageFailure = StageSequence::PageFailure{page.id(), toPageFailureKind(failure), reasons};
    state.failures.addImageFailure(outcome.pageIndex, page, *pageFailure);
  } else if ((state.lastFilterIdx == state.stages.outputFilterIdx())
             && !state.stages.outputFilter()->hasOutputParams(page.id())) {
    pageFailure = StageSequence::PageFailure{page.id(), StageSequence::PageFailure::OUTPUT_NOT_WRITTEN, {}};
    state.failures.addOtherFailure(outcome.pageIndex, *pageFailure);
  }
  const StageSequence::PageProgress::Status status
      = pageFailure ? StageSequence::PageProgress::FAILED : StageSequence::PageProgress::OK;
  const StageSequence::PageFailure* failurePtr = pageFailure ? &*pageFailure : nullptr;
  if (!reportPageProgress(state, page, status, failurePtr)) {
    state.cancelled = true;
    state.stopping = true;
  }
}
}  // namespace

bool StageSequence::processPass(const PageView view,
                                const int lastFilterIdx,
                                const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                                const OutputFileNameGenerator& outFileNameGen,
                                std::set<ImageId>& failedImages,
                                ProcessAllResult& result,
                                const ProgressCallback& progress) const {
  std::vector<PageInfo> pages;
  for (const PageInfo& page : m_pages->toPageSequence(view)) {
    pages.push_back(page);
  }
  PassState state(*this, m_pages, lastFilterIdx, thumbnailCache, outFileNameGen, failedImages, progress,
                  std::move(pages));
  // After what its tasks use, so that leaving early waits for them first.
  QThreadPool pool;
  // REVIEW-NOTE (2026-11): large scans processed several at once -> memory grows with the number of
  // pages at once (about 5 times at 8 threads in the measurement), up to "Out of memory.". Not fixed
  // because that is the price of the speedup, and the number follows the processing threads of the
  // settings, which the user can lower.
  pool.setMaxThreadCount(ThreadBudget::maxThreads());

  while (true) {
    startAvailableTasks(state, pool);
    if (state.running == 0) {
      break;
    }

    const PageOutcome outcome = state.outcomes.waitAndPop();
    --state.running;
    handlePageOutcome(state, outcome);
  }

  if (state.outOfMemoryPage >= 0) {
    const PageInfo& page = state.pages[state.outOfMemoryPage];
    state.failures.addImageFailure(
        state.outOfMemoryPage, page,
        {page.id(), PageFailure::PROCESSING_FAILED, {QCoreApplication::translate("StageSequence", "Out of memory.")}});
  }
  state.failures.appendTo(result.failures);
  if (state.cancelled) {
    result.cancelled = true;
  }
  if (state.exception) {
    std::rethrow_exception(state.exception);
  }
  return !state.stopping;
}

StageSequence::ProcessAllResult StageSequence::processAll(const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                                                          const OutputFileNameGenerator& outFileNameGen,
                                                          const int lastFilterIdx,
                                                          const ProgressCallback& progress,
                                                          const AfterMarginsCallback& afterMargins) const {
  ProcessAllResult result;
  std::set<ImageId> failedImages;
  // The passes of a full run: up to Split, up to Margins, then Output. The bound cuts the last of
  // them: a pass runs for its own step, and only the Output pass for the output filter or above.
  if (!processPass(IMAGE_VIEW, std::min(m_pageSplitFilterIdx, lastFilterIdx), thumbnailCache, outFileNameGen,
                   failedImages, result, progress)) {
    return result;
  }
  if (lastFilterIdx > m_pageSplitFilterIdx) {
    const int marginsLast = std::min(m_pageLayoutFilterIdx, lastFilterIdx);
    if (!processPass(PAGE_VIEW, marginsLast, thumbnailCache, outFileNameGen, failedImages, result, progress)) {
      return result;
    }
    if (afterMargins && (lastFilterIdx >= m_pageLayoutFilterIdx)) {
      if (!afterMargins()) {
        result.cancelled = true;
        return result;
      }
    }
    if ((lastFilterIdx >= m_outputFilterIdx) && !m_pageLayoutFilter->checkReadyForOutput(*m_pages)) {
      return result;
    }
  }
  // The sizes may be known from an earlier run, but the pages of a failed image would keep their
  // old output files, which then pass for current ones.
  if (!result.failures.empty()) {
    return result;
  }
  if (lastFilterIdx >= m_outputFilterIdx) {
    result.output
        = processPass(PAGE_VIEW, m_outputFilterIdx, thumbnailCache, outFileNameGen, failedImages, result, progress);
  }
  return result;
}

std::shared_ptr<CompositeCacheDrivenTask> StageSequence::createCacheDrivenTask(
    const int lastFilterIdx,
    const OutputFileNameGenerator& outFileNameGen) const {
  std::shared_ptr<fix_orientation::CacheDrivenTask> fixOrientationTask;
  std::shared_ptr<page_split::CacheDrivenTask> pageSplitTask;
  std::shared_ptr<deskew::CacheDrivenTask> deskewTask;
  std::shared_ptr<select_content::CacheDrivenTask> selectContentTask;
  std::shared_ptr<page_layout::CacheDrivenTask> pageLayoutTask;
  std::shared_ptr<output::CacheDrivenTask> outputTask;

  if (lastFilterIdx >= m_outputFilterIdx) {
    outputTask = m_outputFilter->createCacheDrivenTask(outFileNameGen);
  }
  if (lastFilterIdx >= m_pageLayoutFilterIdx) {
    pageLayoutTask = m_pageLayoutFilter->createCacheDrivenTask(outputTask);
  }
  if (lastFilterIdx >= m_selectContentFilterIdx) {
    selectContentTask = m_selectContentFilter->createCacheDrivenTask(pageLayoutTask);
  }
  if (lastFilterIdx >= m_deskewFilterIdx) {
    deskewTask = m_deskewFilter->createCacheDrivenTask(selectContentTask);
  }
  if (lastFilterIdx >= m_pageSplitFilterIdx) {
    pageSplitTask = m_pageSplitFilter->createCacheDrivenTask(deskewTask);
  }
  if (lastFilterIdx >= m_fixOrientationFilterIdx) {
    fixOrientationTask = m_fixOrientationFilter->createCacheDrivenTask(pageSplitTask);
  }

  assert(fixOrientationTask);
  return fixOrientationTask;
}  // StageSequence::createCacheDrivenTask

std::vector<ExportPage> StageSequence::pdfExportPages(const ProjectPages& pages,
                                                      const OutputFileNameGenerator& outFileNameGen) const {
  // The pages in project order, independent of how the thumbnails are sorted.
  const QString& outDir = outFileNameGen.outDir();
  const QDir foregroundDir(output::Utils::foregroundDir(outDir));
  const QDir backgroundDir(output::Utils::backgroundDir(outDir));
  // The same check as for the hourglass of the Output step; no image is processed.
  const std::shared_ptr<CompositeCacheDrivenTask> statusTask = createCacheDrivenTask(m_outputFilterIdx, outFileNameGen);
  std::vector<ExportPage> result;
  for (const PageInfo& pageInfo : pages.toPageSequence(PAGE_VIEW)) {
    const PageId& pageId = pageInfo.id();
    const QString fileName = outFileNameGen.fileNameFor(pageId);
    result.emplace_back(outFileNameGen.filePathFor(pageId), foregroundDir.absoluteFilePath(fileName),
                        backgroundDir.absoluteFilePath(fileName), m_outputFilter->isMixedMode(pageId));
    result.back().setPageId(pageId);
    // Only the file headers are read, so this is quick even for many pages.
    result.back().analyze();
    if (result.back().kind() != ExportPage::MISSING) {
      output::OutputStatusCollector status;
      statusTask->process(pageInfo, &status);
      result.back().setOutdated(!status.isUpToDate());
    }
  }
  return result;
}
