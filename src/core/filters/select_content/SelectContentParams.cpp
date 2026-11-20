// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SelectContentParams.h"

#include <AutoManualMode.h>

#include <QDomDocument>
#include <QDomElement>
#include <QSizeF>

#include "DetectionOptions.h"
#include "XmlMarshaller.h"
#include "XmlUnmarshaller.h"

namespace select_content {
SelectContentParams::SelectContentParams()
    : m_pageRectSize(QSizeF(210, 297)),
      m_contentDetectEnabled(true),
      m_pageDetectMode(MODE_DISABLED),
      m_fineTuneCorners(false) {}

SelectContentParams::SelectContentParams(const QSizeF& pageRectSize,
                                         bool contentDetectEnabled,
                                         AutoManualMode pageDetectMode,
                                         bool fineTuneCorners)
    : m_pageRectSize(pageRectSize),
      m_contentDetectEnabled(contentDetectEnabled),
      m_pageDetectMode(pageDetectMode),
      m_fineTuneCorners(fineTuneCorners) {}

SelectContentParams::SelectContentParams(const QDomElement& el)
    : m_pageRectSize(XmlUnmarshaller::sizeF(el.namedItem("pageRectSize").toElement())),
      m_contentDetectEnabled(el.attribute("contentDetectEnabled", "1") != "0"),
      m_pageDetectMode(stringToAutoManualMode(el.attribute("pageDetectMode"))),
      m_fineTuneCorners(el.attribute("fineTuneCorners") == "1"),
      m_detectionOptions(el.namedItem("detection").toElement()) {}

QDomElement SelectContentParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(XmlMarshaller(doc).sizeF(m_pageRectSize, "pageRectSize"));
  el.setAttribute("contentDetectEnabled", m_contentDetectEnabled ? "1" : "0");
  el.setAttribute("pageDetectMode", autoManualModeToString(m_pageDetectMode));
  el.setAttribute("fineTuneCorners", m_fineTuneCorners ? "1" : "0");
  QDomElement detectionEl(doc.createElement("detection"));
  m_detectionOptions.toXml(detectionEl);
  el.appendChild(detectionEl);
  return el;
}

const QSizeF& SelectContentParams::getPageRectSize() const {
  return m_pageRectSize;
}

void SelectContentParams::setPageRectSize(const QSizeF& pageRectSize) {
  m_pageRectSize = pageRectSize;
}

bool SelectContentParams::isContentDetectEnabled() const {
  return m_contentDetectEnabled;
}

void SelectContentParams::setContentDetectEnabled(bool contentDetectEnabled) {
  m_contentDetectEnabled = contentDetectEnabled;
}

bool SelectContentParams::isFineTuneCorners() const {
  return m_fineTuneCorners;
}

void SelectContentParams::setFineTuneCorners(bool fineTuneCorners) {
  m_fineTuneCorners = fineTuneCorners;
}

AutoManualMode SelectContentParams::getPageDetectMode() const {
  return m_pageDetectMode;
}

void SelectContentParams::setPageDetectMode(AutoManualMode pageDetectMode) {
  m_pageDetectMode = pageDetectMode;
}

const DetectionOptions& SelectContentParams::getDetectionOptions() const {
  return m_detectionOptions;
}

void SelectContentParams::setDetectionOptions(const DetectionOptions& options) {
  m_detectionOptions = options;
}
}  // namespace select_content