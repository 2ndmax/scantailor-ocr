// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_FILTER_H_
#define SCANTAILOR_FIX_ORIENTATION_FILTER_H_

#include <memory>

#include "AbstractFilter.h"
#include "FilterResult.h"
#include "NonCopyable.h"
#include "PageView.h"
#include "FixOrientationParams.h"

class ImageId;
class QString;
class QDomDocument;
class QDomElement;
class ImageSettings;

namespace page_split {
class Task;
class CacheDrivenTask;
}  // namespace page_split

namespace fix_orientation {
class Task;
class CacheDrivenTask;
class Settings;

/**
 * \note All methods of this class except the destructor
 *       must be called from the GUI thread only.
 */
class Filter : public AbstractFilter {
  DECLARE_NON_COPYABLE(Filter)

 public:
  Filter();

  ~Filter() override;

  QString getName() const override;

  PageView getView() const override;

  void performRelinking(const AbstractRelinker& relinker) override;

  QDomElement saveSettings(const ProjectIdWriter& writer, QDomDocument& doc) const override;

  void loadSettings(const ProjectIdReader& reader, const QDomElement& filtersEl) override;

  void loadDefaultSettings(const PageInfo& pageInfo, const FixOrientationParams& defaults);

  std::shared_ptr<Task> createTask(const PageId& pageId,
                                   std::shared_ptr<page_split::Task> nextTask,
                                   bool batchProcessing);

  std::shared_ptr<CacheDrivenTask> createCacheDrivenTask(std::shared_ptr<page_split::CacheDrivenTask> nextTask);

  // For the options panel, which FilterUi builds from them.
  const std::shared_ptr<Settings>& settings() const { return m_settings; }

 private:
  void writeParams(QDomDocument& doc, QDomElement& filterEl, const ImageId& imageId, int numericId) const;

  void saveImageSettings(const ProjectIdWriter& writer, QDomDocument& doc, QDomElement& filterEl) const;

  void writeImageParams(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const;

  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<ImageSettings> m_imageSettings;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_FILTER_H_
