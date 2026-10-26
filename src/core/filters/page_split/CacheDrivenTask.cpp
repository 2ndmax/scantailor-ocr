// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CacheDrivenTask.h"

#include <utility>

#include "IncompleteThumbnail.h"
#include "PageInfo.h"
#include "PageLayoutAdapter.h"
#include "ProjectPages.h"
#include "Settings.h"
#include "Thumbnail.h"
#include "Utils.h"
#include "core/AbstractFilterDataCollector.h"
#include "core/ThumbnailCollector.h"
#include "filters/deskew/CacheDrivenTask.h"

namespace page_split {

CacheDrivenTask::CacheDrivenTask(std::shared_ptr<Settings> settings,
                                 std::shared_ptr<ProjectPages> projectPages,
                                 std::shared_ptr<deskew::CacheDrivenTask> nextTask)
    : m_nextTask(std::move(nextTask)), m_settings(std::move(settings)), m_projectPages(std::move(projectPages)) {}

CacheDrivenTask::~CacheDrivenTask() = default;

void CacheDrivenTask::process(const PageInfo& pageInfo,
                              AbstractFilterDataCollector* collector,
                              const ImageTransformation& xform) {
  const Settings::Record record(m_settings->getPageRecord(pageInfo.imageId()));

  const OrthogonalRotation preRotation(xform.preRotation());
  const Dependencies deps(pageInfo.metadata().size(), preRotation, xform.origPreCropArea(),
                          record.combinedLayoutType());

  const Params* params = record.params();

  if (!params || !deps.compatibleWith(*params)) {
    if (auto* thumbCol = dynamic_cast<ThumbnailCollector*>(collector)) {
      thumbCol->processThumbnail(std::unique_ptr<QGraphicsItem>(new IncompleteThumbnail(
          thumbCol->thumbnailCache(), thumbCol->maxLogicalThumbSize(), pageInfo.imageId(), xform)));
    }
    return;
  }

  PageLayout layout(params->pageLayout());
  PageLayoutAdapter::correctPageLayoutType(&layout);
  // m_projectPages controls number of pages displayed in thumbnail list
  // usually this is set in Task, but if user changed layout with Apply To..
  // and just jumped to next stage - the Task::process isn't invoked for all pages
  // so we must additionally ensure here that we display right number of pages.
  m_projectPages->setLayoutTypeFor(pageInfo.id().imageId(), Utils::toPageLayoutType(layout));

  if (m_nextTask) {
    ImageTransformation newXform(xform);
    // REVIEW-NOTE (2026-10): if the sub-page no longer matches the layout type, pageOutline() is empty and the whole
    // image is passed on -> one run may process the full image for a page that is about to vanish. Not fixed because
    // the page list is corrected right after and a reschedule needs pipeline changes.
    const QPolygonF pagePoly(layout.pageOutline(pageInfo.id().subPage()).toPolygon());
    const QPolygonF merged(Utils::intersectPreCropWithPageOutline(pagePoly, newXform.preCropArea()));
    newXform.setPreCropArea(merged);
    m_nextTask->process(pageInfo, collector, newXform);
    return;
  }

  if (auto* thumbCol = dynamic_cast<ThumbnailCollector*>(collector)) {
    thumbCol->processThumbnail(std::unique_ptr<QGraphicsItem>(
        new Thumbnail(thumbCol->thumbnailCache(), thumbCol->maxLogicalThumbSize(), pageInfo.imageId(), xform, layout,
                      pageInfo.leftHalfRemoved(), pageInfo.rightHalfRemoved())));
  }
}  // CacheDrivenTask::process
}  // namespace page_split