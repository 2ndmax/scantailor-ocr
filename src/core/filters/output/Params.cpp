// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Params.h"

#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>
#include <foundation/Utils.h>

#include <cmath>

using namespace foundation;

namespace output {
namespace {
const double DEFAULT_DESPECKLE_LEVEL = 1.0;

// ScanTailor stored the level as a word. The slider values 1 to 3 are the same levels.
double despeckleLevelFromXml(const QString& value) {
  if (value.isEmpty()) {
    return DEFAULT_DESPECKLE_LEVEL;
  }
  bool ok = false;
  const double level = value.toDouble(&ok);
  if (ok && std::isfinite(level)) {
    return level;
  }
  switch (despeckleLevelFromString(value)) {
    case DESPECKLE_OFF:
      return 0.0;
    case DESPECKLE_CAUTIOUS:
      return 1.0;
    case DESPECKLE_NORMAL:
      return 2.0;
    case DESPECKLE_AGGRESSIVE:
      return 3.0;
  }
  return DEFAULT_DESPECKLE_LEVEL;
}
}  // namespace

Params::Params() : m_dpi(600, 600), m_despeckleLevel(DEFAULT_DESPECKLE_LEVEL), m_blackOnWhite(true) {}

Params::Params(const Dpi& dpi,
               const ColorParams& colorParams,
               const SplittingOptions& splittingOptions,
               const PictureShapeOptions& pictureShapeOptions,
               const dewarping::DistortionModel& distortionModel,
               const DepthPerception& depthPerception,
               const DewarpingOptions& dewarpingOptions,
               const double despeckleLevel)
    : m_dpi(dpi),
      m_colorParams(colorParams),
      m_splittingOptions(splittingOptions),
      m_pictureShapeOptions(pictureShapeOptions),
      m_distortionModel(distortionModel),
      m_depthPerception(depthPerception),
      m_dewarpingOptions(dewarpingOptions),
      m_despeckleLevel(despeckleLevel),
      m_blackOnWhite(true) {}

Params::Params(const QDomElement& el)
    : m_dpi(el.namedItem("dpi").toElement()),
      m_colorParams(el.namedItem("color-params").toElement()),
      m_splittingOptions(el.namedItem("splitting").toElement()),
      m_pictureShapeOptions(el.namedItem("picture-shape-options").toElement()),
      m_distortionModel(el.namedItem("distortion-model").toElement()),
      m_depthPerception(el.attribute("depthPerception")),
      m_dewarpingOptions(el.namedItem("dewarping-options").toElement()),
      m_despeckleLevel(despeckleLevelFromXml(el.attribute("despeckleLevel"))),
      m_blackOnWhite(el.attribute("blackOnWhite", "1") != "0") {}

QDomElement Params::toXml(QDomDocument& doc, const QString& name) const {
  XmlMarshaller marshaller(doc);

  QDomElement el(doc.createElement(name));
  el.appendChild(m_distortionModel.toXml(doc, "distortion-model"));
  el.appendChild(m_pictureShapeOptions.toXml(doc, "picture-shape-options"));
  el.setAttribute("depthPerception", m_depthPerception.toString());
  el.appendChild(m_dewarpingOptions.toXml(doc, "dewarping-options"));
  el.setAttribute("despeckleLevel", Utils::doubleToString(m_despeckleLevel));
  el.appendChild(m_dpi.toXml(doc, "dpi"));
  el.appendChild(m_colorParams.toXml(doc, "color-params"));
  el.appendChild(m_splittingOptions.toXml(doc, "splitting"));
  el.setAttribute("blackOnWhite", m_blackOnWhite ? "1" : "0");
  return el;
}
}  // namespace output
