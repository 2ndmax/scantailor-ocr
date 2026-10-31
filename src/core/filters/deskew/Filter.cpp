// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"

#include <OrderByDeviationProvider.h>
#include <filters/select_content/CacheDrivenTask.h>
#include <filters/select_content/Task.h>

#include <utility>

#include "AbstractRelinker.h"
#include "CacheDrivenTask.h"
#include "FilterUiInterface.h"
#include "OptionsWidget.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "Task.h"
#include "Utils.h"

namespace deskew {
Filter::Filter(const PageSelectionAccessor& pageSelectionAccessor,
               std::shared_ptr<ProcessingOptions> processingOptions,
               std::shared_ptr<DefaultParams> defaultParams)
    : m_settings(std::make_shared<Settings>()),
      m_imageSettings(std::make_shared<ImageSettings>()),
      m_selectedPageOrder(0),
      m_processingOptions(std::move(processingOptions)),
      m_defaultParams(std::move(defaultParams)) {
  m_optionsWidget.reset(new OptionsWidget(m_settings, m_defaultParams, pageSelectionAccessor));

  const PageOrderOption::ProviderPtr defaultOrder;
  const auto orderByDeviation = std::make_shared<OrderByDeviationProvider>(m_settings->deviationProvider());
  m_pageOrderOptions.emplace_back(tr("Natural order"), defaultOrder);
  m_pageOrderOptions.emplace_back(tr("Order by decreasing deviation"), orderByDeviation);
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return QCoreApplication::translate("deskew::Filter", "Deskew");
}

PageView Filter::getView() const {
  return PAGE_VIEW;
}

void Filter::performRelinking(const AbstractRelinker& relinker) {
  m_settings->performRelinking(relinker);
  m_imageSettings->performRelinking(relinker);
}

void Filter::preUpdateUI(FilterUiInterface* const ui, const PageInfo& pageInfo) {
  m_optionsWidget->preUpdateUI(pageInfo.id());
  ui->setOptionsWidget(m_optionsWidget.get(), ui->KEEP_OWNERSHIP);
}

QDomElement Filter::saveSettings(const ProjectWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("deskew"));

  int contentPages = 0;
  int topEdgePages = 0;
  writer.enumPages([&](const PageId& pageId, const int numericId) {
    const std::unique_ptr<Params> params(m_settings->getPageParams(pageId));
    if (!params) {
      return;
    }
    this->writeParams(doc, filterEl, *params, numericId);
    if (params->detection() == DETECT_TOP_EDGE) {
      ++topEdgePages;
    } else {
      ++contentPages;
    }
  });
  // The detection method is stored per page. Other ScanTailor versions only know this project-wide
  // attribute, so it gets the method most pages use.
  filterEl.setAttribute("algoContentBased", topEdgePages > contentPages ? "0" : "1");

  saveImageSettings(writer, doc, filterEl);
  return filterEl;
}

void Filter::loadSettings(const ProjectReader& reader, const QDomElement& filtersEl) {
  const QDomElement filterEl(filtersEl.namedItem("deskew").toElement());
  m_settings->load(reader, filterEl);
  m_imageSettings->load(reader, filterEl.namedItem("image-settings").toElement());
}

void Filter::writeParams(QDomDocument& doc, QDomElement& filterEl, const Params& params, int numericId) const {
  QDomElement pageEl(doc.createElement("page"));
  pageEl.setAttribute("id", numericId);
  pageEl.appendChild(params.toXml(doc, "params"));

  filterEl.appendChild(pageEl);
}

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<select_content::Task> nextTask,
                                         const bool batchProcessing,
                                         const bool debug) {
  return std::make_shared<Task>(std::static_pointer_cast<Filter>(shared_from_this()), m_settings, m_imageSettings,
                                std::move(nextTask), pageId, m_processingOptions->blackOnWhiteDetection,
                                m_defaultParams->getDeskewParams().isAutoOblique(),
                                m_defaultParams->getDeskewParams().getDetection(), batchProcessing, debug);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(
    std::shared_ptr<select_content::CacheDrivenTask> nextTask) {
  return std::make_shared<CacheDrivenTask>(m_settings, std::move(nextTask));
}

std::vector<PageOrderOption> Filter::pageOrderOptions() const {
  return m_pageOrderOptions;
}

int Filter::selectedPageOrder() const {
  return m_selectedPageOrder;
}

void Filter::selectPageOrder(int option) {
  assert((unsigned) option < m_pageOrderOptions.size());
  m_selectedPageOrder = option;
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo, const DefaultParams& defaults) {
  if (!m_settings->isParamsNull(pageInfo.id()))
    return;

  m_settings->setPageParams(pageInfo.id(), Utils::buildDefaultParams(defaults));
}

OptionsWidget* Filter::optionsWidget() {
  return m_optionsWidget.get();
}

void Filter::saveImageSettings(const ProjectWriter& writer, QDomDocument& doc, QDomElement& filterEl) const {
  QDomElement imageSettingsEl(doc.createElement("image-settings"));
  writer.enumPages([&](const PageId& pageId, const int numericId) {
    this->writeImageParams(doc, imageSettingsEl, pageId, numericId);
  });

  filterEl.appendChild(imageSettingsEl);
}

void Filter::writeImageParams(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const {
  const std::unique_ptr<ImageSettings::PageParams> params(m_imageSettings->getPageParams(pageId));
  if (!params) {
    return;
  }

  QDomElement pageEl(doc.createElement("page"));
  pageEl.setAttribute("id", numericId);
  pageEl.appendChild(params->toXml(doc, "image-params"));

  filterEl.appendChild(pageEl);
}
}  // namespace deskew
