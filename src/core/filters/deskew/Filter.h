// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_FILTER_H_
#define SCANTAILOR_DESKEW_FILTER_H_

#include <QCoreApplication>
#include <memory>

#include "AbstractFilter.h"

#include "DeskewParams.h"
#include "FilterResult.h"
#include "NonCopyable.h"
#include "PageView.h"
#include "ProcessingOptions.h"
#include "Settings.h"

class QString;
class ImageSettings;

namespace select_content {
class Task;
class CacheDrivenTask;
}  // namespace select_content

namespace deskew {
class Task;
class CacheDrivenTask;
class Settings;

class Filter : public AbstractFilter {
  DECLARE_NON_COPYABLE(Filter)

  Q_DECLARE_TR_FUNCTIONS(deskew::Filter)
 public:
  Filter(std::shared_ptr<ProcessingOptions> processingOptions, std::shared_ptr<const DeskewParams> defaultParams);

  ~Filter() override;

  QString getName() const override;

  PageView getView() const override;

  void performRelinking(const AbstractRelinker& relinker) override;

  QDomElement saveSettings(const ProjectIdWriter& writer, QDomDocument& doc) const override;

  void loadSettings(const ProjectIdReader& reader, const QDomElement& filtersEl) override;

  void loadDefaultSettings(const PageInfo& pageInfo, const DeskewParams& defaults);

  std::shared_ptr<Task> createTask(const PageId& pageId,
                                   std::shared_ptr<select_content::Task> nextTask,
                                   bool batchProcessing,
                                   bool debug);

  std::shared_ptr<CacheDrivenTask> createCacheDrivenTask(std::shared_ptr<select_content::CacheDrivenTask> nextTask);

  /**
   * The parameters of the pages, e.g. for reading the angle found.
   */
  const std::shared_ptr<Settings>& settings() const { return m_settings; }

  // For the options panel, which FilterUi builds from them.
  const std::shared_ptr<const DeskewParams>& defaultParams() const { return m_defaultParams; }

 private:
  void writeParams(QDomDocument& doc, QDomElement& filterEl, const Params& params, int numericId) const;

  void saveImageSettings(const ProjectIdWriter& writer, QDomDocument& doc, QDomElement& filterEl) const;

  void writeImageParams(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const;

  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<ImageSettings> m_imageSettings;
  std::shared_ptr<ProcessingOptions> m_processingOptions;
  // The start values of the project.
  std::shared_ptr<const DeskewParams> m_defaultParams;
};
}  // namespace deskew
#endif  // ifndef SCANTAILOR_DESKEW_FILTER_H_
