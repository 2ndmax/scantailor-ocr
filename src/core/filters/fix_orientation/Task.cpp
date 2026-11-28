// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"

#include <QRect>
#include <utility>

#include "BatchProcessingResult.h"
#include "Filter.h"
#include "Settings.h"
#include "TaskResult.h"
#include "TaskStatus.h"
#include "Utils.h"
#include "page_split/Task.h"

namespace fix_orientation {
using imageproc::BinaryThreshold;

Task::Task(const PageId& pageId,
           std::shared_ptr<Filter> filter,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<ImageSettings> imageSettings,
           std::shared_ptr<page_split::Task> nextTask,
           const bool batchProcessing)
    : m_filter(std::move(filter)),
      m_nextTask(std::move(nextTask)),
      m_settings(std::move(settings)),
      m_imageSettings(std::move(imageSettings)),
      m_pageId(pageId),
      m_imageId(m_pageId.imageId()),
      m_batchProcessing(batchProcessing) {}

Task::~Task() = default;

FilterResultPtr Task::process(const TaskStatus& status, FilterData data) {
  // This function is executed from the worker thread.
  status.throwIfCancelled();

  updateFilterData(data);

  ImageTransformation xform(data.xform());
  Utils::applyRotationAndTrim(xform, m_settings->getRotationFor(m_imageId), m_settings->getTrim(m_imageId));

  if (m_nextTask) {
    return m_nextTask->process(status, FilterData(data, xform));
  } else {
    if (m_batchProcessing) {
      return std::make_shared<BatchProcessingResult>(m_filter, PageId(m_imageId));
    }
    return std::make_shared<TaskResult>(m_filter, data.origImage(), m_imageId, xform);
  }
}

void Task::updateFilterData(FilterData& data) {
  if (const std::unique_ptr<ImageSettings::PageParams> params = m_imageSettings->getPageParams(m_pageId)) {
    data.updateImageParams(*params);
  } else {
    const ImageTrim trim = m_settings->getTrim(m_imageId);
    const QRect trimRect = trim.toInnerRect(data.grayImage().size());
    const QImage otsuImage = (trim.enabled && !trimRect.isEmpty() && trimRect != data.grayImage().rect())
                                 ? data.grayImage().toQImage().copy(trimRect)
                                 : data.grayImage().toQImage();
    ImageSettings::PageParams newParams(BinaryThreshold::otsuThreshold(otsuImage), true);

    m_imageSettings->setPageParams(m_pageId, newParams);
    data.updateImageParams(newParams);
  }
}
}  // namespace fix_orientation
