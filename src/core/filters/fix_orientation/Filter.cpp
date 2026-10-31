// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Filter.h"

#include <filters/page_split/CacheDrivenTask.h>
#include <filters/page_split/Task.h>

#include <QCoreApplication>
#include <utility>

#include "CacheDrivenTask.h"
#include "FilterUiInterface.h"
#include "ImageSettings.h"
#include "ImageTrim.h"
#include "OptionsWidget.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "Settings.h"
#include "Task.h"
#include "Utils.h"

namespace fix_orientation {
Filter::Filter(const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::make_shared<Settings>()), m_imageSettings(std::make_shared<ImageSettings>()) {
  m_optionsWidget.reset(new OptionsWidget(m_settings, pageSelectionAccessor));
}

Filter::~Filter() = default;

QString Filter::getName() const {
  return QCoreApplication::translate("fix_orientation::Filter", "Import");
}

PageView Filter::getView() const {
  return IMAGE_VIEW;
}

void Filter::performRelinking(const AbstractRelinker& relinker) {
  m_settings->performRelinking(relinker);
  m_imageSettings->performRelinking(relinker);
}

void Filter::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  const OrthogonalRotation rotation(m_settings->getRotationFor(pageInfo.id().imageId()));
  m_optionsWidget->preUpdateUI(pageInfo, rotation);
  ui->setOptionsWidget(m_optionsWidget.get(), ui->KEEP_OWNERSHIP);
}

QDomElement Filter::saveSettings(const ProjectWriter& writer, QDomDocument& doc) const {
  QDomElement filterEl(doc.createElement("fix-orientation"));
  writer.enumImages(
      [&](const ImageId& imageId, const int numericId) { this->writeParams(doc, filterEl, imageId, numericId); });

  saveImageSettings(writer, doc, filterEl);
  return filterEl;
}

void Filter::loadSettings(const ProjectReader& reader, const QDomElement& filtersEl) {
  const QDomElement filterEl(filtersEl.namedItem("fix-orientation").toElement());
  m_settings->load(reader, filterEl);
  m_imageSettings->load(reader, filterEl.namedItem("image-settings").toElement());
}

std::shared_ptr<Task> Filter::createTask(const PageId& pageId,
                                         std::shared_ptr<page_split::Task> nextTask,
                                         const bool batchProcessing) {
  return std::make_shared<Task>(pageId, std::static_pointer_cast<Filter>(shared_from_this()), m_settings,
                                m_imageSettings, std::move(nextTask), batchProcessing);
}

std::shared_ptr<CacheDrivenTask> Filter::createCacheDrivenTask(std::shared_ptr<page_split::CacheDrivenTask> nextTask) {
  return std::make_shared<CacheDrivenTask>(m_settings, std::move(nextTask));
}

void Filter::writeParams(QDomDocument& doc, QDomElement& filterEl, const ImageId& imageId, int numericId) const {
  // A rotation of 0 is saved too: an image without one gets the default rotation when loaded.
  const bool hasRotation = !m_settings->isRotationNull(imageId);
  const ImageTrim trim(m_settings->getTrim(imageId));

  if (!hasRotation && !trim.enabled) {
    return;
  }

  QDomElement imageEl(doc.createElement("image"));
  imageEl.setAttribute("id", numericId);
  if (hasRotation) {
    imageEl.appendChild(m_settings->getRotationFor(imageId).toXml(doc, "rotation"));
  }
  if (trim.enabled) {
    QDomElement trimEl(doc.createElement("trim"));
    trimEl.setAttribute("enabled", "1");
    trimEl.setAttribute("left", QString::number(trim.left));
    trimEl.setAttribute("right", QString::number(trim.right));
    trimEl.setAttribute("top", QString::number(trim.top));
    trimEl.setAttribute("bottom", QString::number(trim.bottom));
    imageEl.appendChild(trimEl);
  }
  filterEl.appendChild(imageEl);
}

void Filter::loadDefaultSettings(const PageInfo& pageInfo, const DefaultParams& defaults) {
  if (!m_settings->isRotationNull(pageInfo.id().imageId()))
    return;

  m_settings->applyRotation(pageInfo.id().imageId(), Utils::getDefaultOrthogonalRotation(defaults));
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
}  // namespace fix_orientation
