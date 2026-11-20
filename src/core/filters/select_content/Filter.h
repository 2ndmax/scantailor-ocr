// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_FILTER_H_
#define SCANTAILOR_SELECT_CONTENT_FILTER_H_

#include <QCoreApplication>
#include <memory>
#include <vector>

#include "AbstractFilter.h"

#include "SelectContentParams.h"
#include "Units.h"
#include "FilterResult.h"
#include "NonCopyable.h"
#include "PageOrderOption.h"
#include "PageView.h"
#include "Settings.h"

class QString;

namespace page_layout {
class Task;
class CacheDrivenTask;
}  // namespace page_layout

namespace select_content {
class Task;
class CacheDrivenTask;
class Settings;

class Filter : public AbstractFilter {
  DECLARE_NON_COPYABLE(Filter)

  Q_DECLARE_TR_FUNCTIONS(select_content::Filter)
 public:
  Filter(std::shared_ptr<const SelectContentParams> defaultParams);

  ~Filter() override;

  QString getName() const override;

  PageView getView() const override;

  void performRelinking(const AbstractRelinker& relinker) override;

  QDomElement saveSettings(const ProjectIdWriter& writer, QDomDocument& doc) const override;

  void loadSettings(const ProjectIdReader& reader, const QDomElement& filtersEl) override;

  void loadDefaultSettings(const PageInfo& pageInfo, const SelectContentParams& defaults, Units units);

  std::shared_ptr<Task> createTask(const PageId& pageId,
                                   std::shared_ptr<page_layout::Task> nextTask,
                                   bool batch,
                                   bool debug);

  std::shared_ptr<CacheDrivenTask> createCacheDrivenTask(std::shared_ptr<page_layout::CacheDrivenTask> nextTask);

  /**
   * The parameters of the pages, e.g. for reading the content box found.
   */
  const std::shared_ptr<Settings>& settings() const { return m_settings; }

  // For the options panel, which FilterUi builds from them.
  const std::shared_ptr<const SelectContentParams>& defaultParams() const { return m_defaultParams; }

 private:
  void writePageSettings(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const;

  std::shared_ptr<Settings> m_settings;
  // The start values of the project.
  std::shared_ptr<const SelectContentParams> m_defaultParams;
};
}  // namespace select_content
#endif  // ifndef SCANTAILOR_SELECT_CONTENT_FILTER_H_
