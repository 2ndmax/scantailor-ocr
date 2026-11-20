// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DefaultOutputParams.h"

#include <Dpi.h>

#include <QDomDocument>
#include <QDomElement>

#include "ColorParams.h"
#include "DepthPerception.h"
#include "DespeckleLevel.h"
#include "DewarpingOptions.h"
#include "Params.h"
#include "PictureShapeOptions.h"
#include "SplittingOptions.h"
#include "XmlMarshaller.h"
#include "XmlUnmarshaller.h"

#include <foundation/Utils.h>

namespace {
double despeckleLevelFromXml(const QDomElement& el) {
  return XmlUnmarshaller::number(el, "despeckleLevel", 1.0);
}
}  // namespace

namespace output {
DefaultOutputParams::DefaultOutputParams() : m_dpi(600, 600), m_despeckleLevel(1.0) {}

DefaultOutputParams::DefaultOutputParams(const Dpi& dpi,
                                         const ColorParams& colorParams,
                                         const SplittingOptions& splittingOptions,
                                         const PictureShapeOptions& pictureShapeOptions,
                                         const DepthPerception& depthPerception,
                                         const DewarpingOptions& dewarpingOptions,
                                         double despeckleLevel)
    : m_dpi(dpi),
      m_colorParams(colorParams),
      m_splittingOptions(splittingOptions),
      m_pictureShapeOptions(pictureShapeOptions),
      m_depthPerception(depthPerception),
      m_dewarpingOptions(dewarpingOptions),
      m_despeckleLevel(despeckleLevel) {}

DefaultOutputParams::DefaultOutputParams(const QDomElement& el)
    : m_dpi(Params::outputDpiFromXml(el.namedItem("dpi").toElement())),
      m_colorParams(el.namedItem("colorParams").toElement()),
      m_splittingOptions(el.namedItem("splittingOptions").toElement()),
      m_pictureShapeOptions(el.namedItem("pictureShapeOptions").toElement()),
      m_depthPerception(el.attribute("depthPerception")),
      m_dewarpingOptions(el.namedItem("dewarpingOptions").toElement()),
      m_despeckleLevel(despeckleLevelFromXml(el)) {}

QDomElement DefaultOutputParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_dpi.toXml(doc, "dpi"));
  el.appendChild(m_colorParams.toXml(doc, "colorParams"));
  el.appendChild(m_splittingOptions.toXml(doc, "splittingOptions"));
  el.appendChild(m_pictureShapeOptions.toXml(doc, "pictureShapeOptions"));
  el.setAttribute("depthPerception", foundation::Utils::doubleToString(m_depthPerception.value()));
  el.appendChild(m_dewarpingOptions.toXml(doc, "dewarpingOptions"));
  el.setAttribute("despeckleLevel", foundation::Utils::doubleToString(m_despeckleLevel));
  return el;
}

const Dpi& DefaultOutputParams::getDpi() const {
  return m_dpi;
}

void DefaultOutputParams::setDpi(const Dpi& dpi) {
  m_dpi = dpi;
}

const ColorParams& DefaultOutputParams::getColorParams() const {
  return m_colorParams;
}

void DefaultOutputParams::setColorParams(const ColorParams& colorParams) {
  m_colorParams = colorParams;
}

const SplittingOptions& DefaultOutputParams::getSplittingOptions() const {
  return m_splittingOptions;
}

void DefaultOutputParams::setSplittingOptions(const SplittingOptions& splittingOptions) {
  m_splittingOptions = splittingOptions;
}

const PictureShapeOptions& DefaultOutputParams::getPictureShapeOptions() const {
  return m_pictureShapeOptions;
}

void DefaultOutputParams::setPictureShapeOptions(const PictureShapeOptions& pictureShapeOptions) {
  m_pictureShapeOptions = pictureShapeOptions;
}

const DepthPerception& DefaultOutputParams::getDepthPerception() const {
  return m_depthPerception;
}

void DefaultOutputParams::setDepthPerception(const DepthPerception& depthPerception) {
  m_depthPerception = depthPerception;
}

const DewarpingOptions& DefaultOutputParams::getDewarpingOptions() const {
  return m_dewarpingOptions;
}

void DefaultOutputParams::setDewarpingOptions(const DewarpingOptions& dewarpingOptions) {
  m_dewarpingOptions = dewarpingOptions;
}

double DefaultOutputParams::getDespeckleLevel() const {
  return m_despeckleLevel;
}

void DefaultOutputParams::setDespeckleLevel(double despeckleLevel) {
  m_despeckleLevel = despeckleLevel;
}
}  // namespace output