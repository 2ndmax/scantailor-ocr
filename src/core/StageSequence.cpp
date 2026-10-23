// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StageSequence.h"

#include <cassert>
#include <set>

#include <QDir>

#include "LoadFileTask.h"
#include "OutputFileNameGenerator.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
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

StageSequence::StageSequence(const std::shared_ptr<ProjectPages>& pages,
                             const PageSelectionAccessor& pageSelectionAccessor)
    : m_pages(pages),
      m_processingOptions(std::make_shared<ProcessingOptions>()),
      m_defaultParams(std::make_shared<DefaultParams>()),
      m_fixOrientationFilter(std::make_shared<fix_orientation::Filter>(pageSelectionAccessor)),
      m_pageSplitFilter(std::make_shared<page_split::Filter>(pages, pageSelectionAccessor)),
      m_deskewFilter(std::make_shared<deskew::Filter>(pageSelectionAccessor, m_processingOptions, m_defaultParams)),
      m_selectContentFilter(std::make_shared<select_content::Filter>(pageSelectionAccessor, m_defaultParams)),
      m_pageLayoutFilter(std::make_shared<page_layout::Filter>(pages, pageSelectionAccessor)),
      m_outputFilter(std::make_shared<output::Filter>(pageSelectionAccessor, m_processingOptions)) {
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
  m_pageLayoutFilter->loadNewProjectSettings(*m_defaultParams);
}

void StageSequence::loadProject(const ProjectReader& reader, const DefaultParams& fallback) {
  *m_processingOptions = reader.readProcessingOptions(fallback.getProcessingOptions());
  std::unique_ptr<DefaultParams> startValues = reader.readDefaultParams();
  *m_defaultParams = startValues ? *startValues : fallback;
  reader.readFilterSettings(m_filters);
}

void StageSequence::loadDefaultSettings(const PageInfo& page) const {
  for (const FilterPtr& filter : m_filters) {
    filter->loadDefaultSettings(page, *m_defaultParams);
  }
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
    debug = false;
  }
  if (lastFilterIdx >= m_fixOrientationFilterIdx) {
    fixOrientationTask = m_fixOrientationFilter->createTask(page.id(), pageSplitTask, batch);
    debug = false;
  }
  assert(fixOrientationTask);
  return std::make_shared<LoadFileTask>(batch ? BackgroundTask::BATCH : BackgroundTask::INTERACTIVE, page,
                                        thumbnailCache, pages, fixOrientationTask);
}  // StageSequence::createTask

StageSequence::ProcessAllResult StageSequence::processAll(const std::shared_ptr<ThumbnailPixmapCache>& thumbnailCache,
                                                          const OutputFileNameGenerator& outFileNameGen,
                                                          const ProgressCallback& progress) const {
  ProcessAllResult result;
  std::set<ImageId> failedImages;
  // Returns false if cancelled.
  const auto process = [&](const PageView view, const int lastFilterIdx) {
    const PageSequence sequence = m_pages->toPageSequence(view);
    const int total = static_cast<int>(sequence.numPages());
    int done = 0;
    for (const PageInfo& page : sequence) {
      if (failedImages.count(page.imageId()) == 0) {
        // The pages that only exist after the split get their start values in the second pass;
        // in the output pass all pages have their parameters.
        if (lastFilterIdx != m_outputFilterIdx) {
          loadDefaultSettings(page);
        }
        const FilterResultPtr taskResult
            = (*createTask(page, lastFilterIdx, thumbnailCache, outFileNameGen, m_pages, true, false))();
        QStringList reasons;
        bool processingFailed = false;
        if (LoadFileTask::isFailure(taskResult, &reasons, &processingFailed)) {
          failedImages.insert(page.imageId());
          result.failures.push_back(
              {page.id(), processingFailed ? PageFailure::PROCESSING_FAILED : PageFailure::LOAD_FAILED, reasons});
        } else if ((lastFilterIdx == m_outputFilterIdx) && !m_outputFilter->hasOutputParams(page.id())) {
          result.failures.push_back({page.id(), PageFailure::OUTPUT_NOT_WRITTEN, {}});
        }
      }
      ++done;
      const bool lastPage = (lastFilterIdx == m_outputFilterIdx) && (done == total);
      if (progress && !progress(lastFilterIdx, done, total) && !lastPage) {
        result.cancelled = true;
        return false;
      }
    }
    return true;
  };
  if (!process(IMAGE_VIEW, m_pageSplitFilterIdx) || !process(PAGE_VIEW, m_pageLayoutFilterIdx)
      || !m_pageLayoutFilter->checkReadyForOutput(*m_pages)) {
    return result;
  }
  result.output = process(PAGE_VIEW, m_outputFilterIdx);
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

std::vector<PdfExportPage> StageSequence::pdfExportPages(const ProjectPages& pages,
                                                         const OutputFileNameGenerator& outFileNameGen) const {
  // The pages in project order, independent of how the thumbnails are sorted.
  const QString outDir = outFileNameGen.outDir();
  const QDir foregroundDir(output::Utils::foregroundDir(outDir));
  const QDir backgroundDir(output::Utils::backgroundDir(outDir));
  // The same check as for the hourglass of the Output step; no image is processed.
  const std::shared_ptr<CompositeCacheDrivenTask> statusTask = createCacheDrivenTask(m_outputFilterIdx, outFileNameGen);
  std::vector<PdfExportPage> result;
  for (const PageInfo& pageInfo : pages.toPageSequence(PAGE_VIEW)) {
    const PageId& pageId = pageInfo.id();
    const QString fileName = outFileNameGen.fileNameFor(pageId);
    result.emplace_back(outFileNameGen.filePathFor(pageId), foregroundDir.absoluteFilePath(fileName),
                        backgroundDir.absoluteFilePath(fileName), m_outputFilter->isMixedMode(pageId));
    // Only the file headers are read, so this is quick even for many pages.
    result.back().analyze();
    if (result.back().kind() != PdfExportPage::MISSING) {
      output::OutputStatusCollector status;
      statusTask->process(pageInfo, &status);
      result.back().setOutdated(!status.isUpToDate());
    }
  }
  return result;
}
