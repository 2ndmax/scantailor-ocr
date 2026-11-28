// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DefaultParams.h"

#include <filters/deskew/Params.h>
#include <filters/output/Params.h>
#include <foundation/Utils.h>

#include <cmath>

#include "Dpi.h"
#include "UnitsConverter.h"
#include "XmlMarshaller.h"
#include "XmlUnmarshaller.h"

using namespace foundation;
using namespace page_split;
using namespace output;

DefaultParams::DefaultParams(const DefaultParams::FixOrientationParams& fixOrientationParams,
                             const DefaultParams::DeskewParams& deskewParams,
                             const DefaultParams::PageSplitParams& pageSplitParams,
                             const DefaultParams::SelectContentParams& selectContentParams,
                             const DefaultParams::PageLayoutParams& pageLayoutParams,
                             const DefaultParams::OutputParams& outputParams)
    : m_fixOrientationParams(fixOrientationParams),
      m_deskewParams(deskewParams),
      m_pageSplitParams(pageSplitParams),
      m_selectContentParams(selectContentParams),
      m_pageLayoutParams(pageLayoutParams),
      m_outputParams(outputParams),
      m_units(MILLIMETRES) {}

DefaultParams::DefaultParams(const QDomElement& el)
    : m_fixOrientationParams(el.namedItem("fix-orientation-params").toElement()),
      m_deskewParams(el.namedItem("deskew-params").toElement()),
      m_pageSplitParams(el.namedItem("page-split-params").toElement()),
      m_selectContentParams(el.namedItem("select-content-params").toElement()),
      m_pageLayoutParams(el.namedItem("page-layout-params").toElement()),
      m_outputParams(el.namedItem("output-params").toElement()),
      m_units(unitsFromString(el.attribute("units"))),
      // Profiles from before these settings have none: the built-in defaults.
      m_processingOptions(el.namedItem("processing-options").toElement(), ProcessingOptions()) {}

QDomElement DefaultParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_fixOrientationParams.toXml(doc, "fix-orientation-params"));
  el.appendChild(m_deskewParams.toXml(doc, "deskew-params"));
  el.appendChild(m_pageSplitParams.toXml(doc, "page-split-params"));
  el.appendChild(m_selectContentParams.toXml(doc, "select-content-params"));
  el.appendChild(m_pageLayoutParams.toXml(doc, "page-layout-params"));
  el.appendChild(m_outputParams.toXml(doc, "output-params"));
  el.appendChild(m_processingOptions.toXml(doc, "processing-options"));
  el.setAttribute("units", unitsToString(m_units));
  return el;
}

DefaultParams::DefaultParams() : m_units(MILLIMETRES) {}

Units DefaultParams::displayUnits(const Units profileUnits, const Units programUnits) {
  return ((profileUnits == PIXELS) || (programUnits == PIXELS)) ? profileUnits : programUnits;
}

DefaultParams DefaultParams::inUnits(const Units units) const {
  DefaultParams converted(*this);
  if ((units == m_units) || (m_units == PIXELS) || (units == PIXELS)) {
    return converted;
  }

  // Millimeters, centimeters and inches convert without a resolution.
  const UnitsConverter converter((Dpi()));
  const auto convert = [&](QSizeF size) {
    double width = size.width();
    double height = size.height();
    converter.convert(width, height, m_units, units);
    return QSizeF(width, height);
  };

  SelectContentParams selectContentParams(m_selectContentParams);
  selectContentParams.setPageRectSize(convert(selectContentParams.getPageRectSize()));
  converted.setSelectContentParams(selectContentParams);

  PageLayoutParams pageLayoutParams(m_pageLayoutParams);
  const Margins& margins = pageLayoutParams.getHardMargins();
  const QSizeF leftTop = convert(QSizeF(margins.left(), margins.top()));
  const QSizeF rightBottom = convert(QSizeF(margins.right(), margins.bottom()));
  pageLayoutParams.setHardMargins(
      Margins(leftTop.width(), leftTop.height(), rightBottom.width(), rightBottom.height()));
  page_layout::OutputSizeOptions outputSizeOptions(pageLayoutParams.getOutputSizeOptions());
  outputSizeOptions.setTargetSizeMM(convert(outputSizeOptions.targetSizeMM()));
  pageLayoutParams.setOutputSizeOptions(outputSizeOptions);
  converted.setPageLayoutParams(pageLayoutParams);

  converted.setUnits(units);
  return converted;
}
