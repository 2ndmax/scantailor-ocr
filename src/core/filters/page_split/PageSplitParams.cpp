// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageSplitParams.h"

#include <Qt>

#include <QDomDocument>
#include <QDomElement>

#include "LayoutType.h"
#include "SplitDetectionMethod.h"

namespace page_split {
PageSplitParams::PageSplitParams(LayoutType layoutType)
    : m_layoutType(layoutType), m_pageOrder(Qt::LeftToRight), m_detectionMethod(SPINE_LINE_DETECTION) {}

PageSplitParams::PageSplitParams()
    : m_layoutType(AUTO_LAYOUT_TYPE), m_pageOrder(Qt::LeftToRight), m_detectionMethod(SPINE_LINE_DETECTION) {}

PageSplitParams::PageSplitParams(const QDomElement& el)
    : m_layoutType(layoutTypeFromString(el.attribute("layoutType"))),
      m_pageOrder(el.attribute("pageOrder") == "RTL" ? Qt::RightToLeft : Qt::LeftToRight),
      m_detectionMethod(el.hasAttribute("detectionMethod")
                            ? splitDetectionMethodFromString(el.attribute("detectionMethod"))
                            : SPINE_LINE_DETECTION) {}

QDomElement PageSplitParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("layoutType", layoutTypeToString(m_layoutType));
  el.setAttribute("pageOrder", m_pageOrder == Qt::RightToLeft ? "RTL" : "LTR");
  el.setAttribute("detectionMethod", splitDetectionMethodToString(m_detectionMethod));
  return el;
}

LayoutType PageSplitParams::getLayoutType() const {
  return m_layoutType;
}

void PageSplitParams::setLayoutType(LayoutType layoutType) {
  m_layoutType = layoutType;
}

Qt::LayoutDirection PageSplitParams::getPageOrder() const {
  return m_pageOrder;
}

void PageSplitParams::setPageOrder(Qt::LayoutDirection pageOrder) {
  m_pageOrder = pageOrder;
}

SplitDetectionMethod PageSplitParams::getDetectionMethod() const {
  return m_detectionMethod;
}

void PageSplitParams::setDetectionMethod(SplitDetectionMethod method) {
  m_detectionMethod = method;
}
}  // namespace page_split