// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ThumbnailFactory.h"

#include <QGraphicsItem>
#include <utility>

#include "CompositeCacheDrivenTask.h"
#include "FilterUiSequence.h"
#include "ThumbnailCollector.h"
#include "ThumbnailDataBase.h"

class ThumbnailFactory::Collector : public ThumbnailCollector {
 public:
  void processThumbnail(std::unique_ptr<ThumbnailDataBase> thumbnail) override { m_thumbnail = std::move(thumbnail); }

  std::unique_ptr<ThumbnailDataBase> retrieveThumbnail() { return std::move(m_thumbnail); }

 private:
  std::unique_ptr<ThumbnailDataBase> m_thumbnail;
};


ThumbnailFactory::ThumbnailFactory(std::shared_ptr<ThumbnailPixmapCache> pixmapCache,
                                   const QSizeF& maxSize,
                                   std::shared_ptr<CompositeCacheDrivenTask> task,
                                   std::shared_ptr<const FilterUiSequence> filterUis)
    : m_pixmapCache(std::move(pixmapCache)),
      m_maxSize(maxSize),
      m_task(std::move(task)),
      m_filterUis(std::move(filterUis)) {}

ThumbnailFactory::~ThumbnailFactory() = default;

std::unique_ptr<QGraphicsItem> ThumbnailFactory::get(const PageInfo& pageInfo) {
  Collector collector;
  m_task->process(pageInfo, &collector);
  const std::unique_ptr<ThumbnailDataBase> thumbnail = collector.retrieveThumbnail();
  if (!thumbnail) {
    return nullptr;
  }
  return m_filterUis->createThumbnail(*thumbnail, m_pixmapCache, m_maxSize);
}
