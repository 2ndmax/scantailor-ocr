// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"

#include <ApplicationSettings.h>
#include <Dpm.h>
#include <ImageLoader.h>
#include <OrderByCompletenessProvider.h>
#include <Transform.h>
#ifdef ENABLE_OCR
#include <ColorModeDetector.h>
#endif

#include <utility>

#include "CacheDrivenTask.h"
#include "FilterUiInterface.h"
#include "GeometryCollector.h"
#include "ImageTransformation.h"
#include "OptionsWidget.h"
#include "OutputGenerator.h"
#include "PageInfo.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "Settings.h"
#include "Task.h"
#include "ThumbnailPixmapCache.h"
#include "Utils.h"

namespace output {
Filter::Filter(const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::make_shared<Settings>()), m_selectedPageOrder(0) {
  m_optionsWidget.reset(new OptionsWidget(m_settings, pageSelectionAccessor));

  const PageOrderOption::ProviderPtr defaultOrder;
  const auto orderByCompleteness = std::make_shared<OrderByCompletenessProvider>();
  m_pageOrderOptions.emplace_back(tr("Natural order"), defaultOrder);
  m_pageOrderOptions.emplace_back(tr("Order by completeness"), orderByCompleteness);
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return QCoreApplication::translate("output::Filter", "Output");
}

PageView Filter::getView() const {
  return PAGE_VIEW;
}

void Filter::performRelinking(const AbstractRelinker& relinker) {
  m_settings->performRelinking(relinker);
}

void Filter::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  m_optionsWidget->preUpdateUI(pageInfo.id());
  ui->setOptionsWidget(m_optionsWidget.get(), ui->KEEP_OWNERSHIP);
}

QDomElement Filter::saveSettings(const ProjectWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("output"));

  writer.enumPages(
      [&](const PageId& pageId, int numericId) { this->writePageSettings(doc, filterEl, pageId, numericId); });
  return filterEl;
}

void Filter::writePageSettings(QDomDocument& doc, QDomElement& filterEl, const PageId& pageId, int numericId) const {
  const Params params(m_settings->getParams(pageId));

  QDomElement pageEl(doc.createElement("page"));
  pageEl.setAttribute("id", numericId);

  pageEl.appendChild(m_settings->pictureZonesForPage(pageId).toXml(doc, "zones"));
  pageEl.appendChild(m_settings->fillZonesForPage(pageId).toXml(doc, "fill-zones"));
  pageEl.appendChild(params.toXml(doc, "params"));
  pageEl.appendChild(m_settings->getOutputProcessingParams(pageId).toXml(doc, "processing-params"));

  std::unique_ptr<OutputParams> outputParams(m_settings->getOutputParams(pageId));
  if (outputParams) {
    pageEl.appendChild(outputParams->toXml(doc, "output-params"));
  }

  filterEl.appendChild(pageEl);
}

void Filter::loadSettings(const ProjectReader& reader, const QDomElement& filtersEl) {
  m_settings->clear();

  const QDomElement filterEl(filtersEl.namedItem("output").toElement());

  const QString pageTagName("page");
  QDomNode node(filterEl.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != pageTagName) {
      continue;
    }
    const QDomElement el(node.toElement());

    bool ok = true;
    const int id = el.attribute("id").toInt(&ok);
    if (!ok) {
      continue;
    }

    const PageId pageId(reader.pageId(id));
    if (pageId.isNull()) {
      continue;
    }

    const ZoneSet pictureZones(el.namedItem("zones").toElement(), m_pictureZonePropFactory);
    if (!pictureZones.empty()) {
      m_settings->setPictureZones(pageId, pictureZones);
    }

    const ZoneSet fillZones(el.namedItem("fill-zones").toElement(), m_fillZonePropFactory);
    if (!fillZones.empty()) {
      m_settings->setFillZones(pageId, fillZones);
    }

    const QDomElement paramsEl(el.namedItem("params").toElement());
    if (!paramsEl.isNull()) {
      const Params params(paramsEl);
      m_settings->setParams(pageId, params);
    }

    const QDomElement outputProcessingParamsEl(el.namedItem("processing-params").toElement());
    if (!outputProcessingParamsEl.isNull()) {
      const OutputProcessingParams outputProcessingParams(outputProcessingParamsEl);
      m_settings->setOutputProcessingParams(pageId, outputProcessingParams);
    }

    const QDomElement outputParamsEl(el.namedItem("output-params").toElement());
    if (!outputParamsEl.isNull()) {
      const OutputParams outputParams(outputParamsEl);
      m_settings->setOutputParams(pageId, outputParams);
    }
  }
}  // Filter::loadSettings

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                                         const OutputFileNameGenerator& outFileNameGen,
                                         const bool batch,
                                         const bool debug) {
  ImageViewTab lastTab(TAB_OUTPUT);
  if (m_optionsWidget.get() != nullptr) {
    lastTab = m_optionsWidget->lastTab();
  }
  return std::make_shared<Task>(std::static_pointer_cast<Filter>(shared_from_this()), m_settings,
                                std::move(thumbnailCache), pageId, outFileNameGen, lastTab, batch, debug);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(const OutputFileNameGenerator& outFileNameGen) {
  return std::make_shared<CacheDrivenTask>(m_settings, outFileNameGen);
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo) {
  if (!m_settings->isParamsNull(pageInfo.id()))
    return;

  m_settings->setParams(pageInfo.id(), Utils::buildDefaultParams());
}

OptionsWidget* Filter::optionsWidget() {
  return m_optionsWidget.get();
}

bool Filter::isMixedMode(const PageId& pageId) const {
  return m_settings->getParams(pageId).colorParams().colorMode() == MIXED;
}

bool Filter::detectColorMode(const PageInfo& pageInfo,
                             const GeometryCollector& geometry,
                             const TaskStatus& status,
                             ColorMode* detectedMode) {
#ifdef ENABLE_OCR
  const QImage source = ImageLoader::load(pageInfo.id().imageId());
  if (source.isNull()) {
    return false;
  }

  // The content area at 300 dpi, independent of the output resolution.
  const Dpi detectionDpi(300, 300);
  ImageTransformation xform(geometry.xform());
  xform.postScaleToDpi(detectionDpi);
  const QRect contentRect = OutputGenerator(xform, geometry.contentRectPhys()).outputContentRect();

  ColorMode mode = BLACK_AND_WHITE;  // A page without content stays black and white.
  if ((contentRect.width() >= 16) && (contentRect.height() >= 16)) {
    QImage content = imageproc::transform(source, xform.transform(), contentRect,
                                          imageproc::OutsidePixels::assumeWeakColor(Qt::white));
    const Dpm dpm(detectionDpi);
    content.setDotsPerMeterX(dpm.horizontal());
    content.setDotsPerMeterY(dpm.vertical());

    const ApplicationSettings& appSettings = ApplicationSettings::getInstance();
    ColorModeDetector::Options options;
    options.minColorShare = appSettings.getColorDetectionMinColor() / 100.0;
    options.usePictureZones = appSettings.isColorDetectionZones();
    options.minPictureShare = appSettings.getColorDetectionMinPicture() / 100.0;
    switch (ColorModeDetector::detect(content, options, status)) {
      case ColorModeDetector::Mode::BlackWhite:
        mode = BLACK_AND_WHITE;
        break;
      case ColorModeDetector::Mode::Mixed:
        mode = MIXED;
        break;
      case ColorModeDetector::Mode::Grayscale:
      case ColorModeDetector::Mode::Color:
        mode = COLOR_GRAYSCALE;
        break;
    }
  }

  ColorParams colorParams = m_settings->getParams(pageInfo.id()).colorParams();
  colorParams.setColorMode(mode);
  m_settings->setColorParams(pageInfo.id(), colorParams);
  if (detectedMode) {
    *detectedMode = mode;
  }
  return true;
#else
  Q_UNUSED(pageInfo);
  Q_UNUSED(geometry);
  Q_UNUSED(status);
  Q_UNUSED(detectedMode);
  return false;
#endif
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
}  // namespace output