// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_STAGESEQUENCE_H_
#define SCANTAILOR_CORE_STAGESEQUENCE_H_

#include <memory>
#include <vector>

#include "AbstractFilter.h"
#include "NonCopyable.h"
#include "filters/deskew/Filter.h"
#include "filters/fix_orientation/Filter.h"
#include "filters/output/Filter.h"
#include "filters/page_layout/Filter.h"
#include "filters/page_split/Filter.h"
#include "filters/select_content/Filter.h"

class PageId;
class ProjectPages;
class PageSelectionAccessor;
class AbstractRelinker;

class StageSequence {
  DECLARE_NON_COPYABLE(StageSequence)

 public:
  using FilterPtr = std::shared_ptr<AbstractFilter>;

  StageSequence(const std::shared_ptr<ProjectPages>& pages, const PageSelectionAccessor& pageSelectionAccessor);

  virtual ~StageSequence() = default;

  void performRelinking(const AbstractRelinker& relinker);

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
