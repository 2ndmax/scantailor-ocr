// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"
#include "PageInfo.h"
#include "SelectContentParams.h"
#include "Units.h"

#include <OrderByDeviationProvider.h>
#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>
#include "../page_layout/CacheDrivenTask.h"
#include "../page_layout/Task.h"
#include <foundation/Utils.h>

#include <utility>

#include "CacheDrivenTask.h"
#include "OrderBySizeProvider.h"
#include "ProjectIdReader.h"
#include "ProjectIdWriter.h"
#include "Task.h"
#include "Utils.h"

using namespace foundation;

namespace select_content {
Filter::Filter(std::shared_ptr<SelectContentParams> defaultParams)
    : m_settings(std::make_shared<Settings>()), m_defaultParams(std::move(defaultParams)) {
  const PageOrderOption::ProviderPtr defaultOrder;
  const auto orderByWidth = std::make_shared<OrderBySizeProvider>(m_settings, OrderBySizeProvider::WIDTH);
  const auto orderByHeight = std::make_shared<OrderBySizeProvider>(m_settings, OrderBySizeProvider::HEIGHT);
  const auto orderByDeviation = std::make_shared<OrderByDeviationProvider>(m_settings->deviationProvider());
  addPageOrderOption({tr("Natural order"), defaultOrder});
  addPageOrderOption({tr("Order by increasing width"), orderByWidth});
  addPageOrderOption({tr("Order by increasing height"), orderByHeight});
  addPageOrderOption({tr("Order by decreasing deviation"), orderByDeviation});
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return tr("Select Content");
}

PageView Filter::getView() const {
  return PAGE_VIEW;
}

void Filter::performRelinking(const AbstractRelinker& relinker) {
  m_settings->performRelinking(relinker);
}

QDomElement Filter::saveSettings(const ProjectIdWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("select-content"));

  filterEl.appendChild(XmlMarshaller(doc).sizeF(m_settings->pageDetectionBox(), "page-detection-box"));
  filterEl.setAttribute("pageDetectionTolerance",
                        foundation::Utils::doubleToString(m_settings->pageDetectionTolerance()));

  writer.enumPages(
      [&](const PageId& pageId, int numericId) { this->writePageSettings(doc, filterEl, pageId, numericId); });
  return filterEl;
}

void Filter::writePageSettings(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const {
  const std::unique_ptr<Params> params(m_settings->getPageParams(pageId));
  if (!params) {
    return;
  }

  QDomElement pageEl(doc.createElement("page"));
  pageEl.setAttribute("id", numericId);
  pageEl.appendChild(params->toXml(doc, "params"));

  filterEl.appendChild(pageEl);
}

void Filter::loadSettings(const ProjectIdReader& reader, const QDomElement& filtersEl) {
  const QDomElement filterEl(filtersEl.namedItem("select-content").toElement());
  m_settings->load(reader, filterEl);
}

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<page_layout::Task> nextTask,
                                         bool batch,
                                         bool debug) {
  return std::make_shared<Task>(std::static_pointer_cast<Filter>(shared_from_this()), std::move(nextTask), m_settings,
                                pageId, m_defaultParams->getDetectionOptions(), batch, debug);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(std::shared_ptr<page_layout::CacheDrivenTask> nextTask) {
  return std::make_shared<CacheDrivenTask>(m_settings, std::move(nextTask));
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo, const SelectContentParams& defaults, Units units) {
  if (!m_settings->isParamsNull(pageInfo.id()))
    return;

  m_settings->setPageParams(pageInfo.id(),
                            select_content::Utils::buildDefaultParams(pageInfo.metadata().dpi(), defaults, units));
}

}  // namespace select_content
