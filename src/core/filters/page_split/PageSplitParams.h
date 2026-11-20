// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_PAGESPLITPARAMS_H_
#define SCANTAILOR_PAGE_SPLIT_PAGESPLITPARAMS_H_

#include <Qt>

#include <QDomDocument>
#include <QDomElement>

#include "LayoutType.h"
#include "SplitDetectionMethod.h"

namespace page_split {
class PageSplitParams {
 public:
  PageSplitParams();

  explicit PageSplitParams(LayoutType layoutType);

  explicit PageSplitParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  LayoutType getLayoutType() const;

  void setLayoutType(LayoutType layoutType);

  Qt::LayoutDirection getPageOrder() const;

  void setPageOrder(Qt::LayoutDirection pageOrder);

  SplitDetectionMethod getDetectionMethod() const;

  void setDetectionMethod(SplitDetectionMethod method);

 private:
  LayoutType m_layoutType;
  Qt::LayoutDirection m_pageOrder;
  SplitDetectionMethod m_detectionMethod;
};
}  // namespace page_split

#endif  // ifndef SCANTAILOR_PAGE_SPLIT_PAGESPLITPARAMS_H_