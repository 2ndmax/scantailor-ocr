// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DeskewParams.h"

#include <AutoManualMode.h>

#include <foundation/Utils.h>

#include <QDomDocument>
#include <QDomElement>

#include "Params.h"
#include "SkewDetection.h"
#include "Utils.h"

using namespace foundation;

namespace deskew {
DeskewParams::DeskewParams(const double deskewAngleDeg,
                           const AutoManualMode mode,
                           const bool autoOblique)
    : m_deskewAngleDeg(deskewAngleDeg), m_mode(mode), m_autoOblique(autoOblique), m_detection(DETECT_CONTENT) {}

DeskewParams::DeskewParams()
    : m_deskewAngleDeg(0.0), m_mode(MODE_AUTO), m_autoOblique(false), m_detection(DETECT_CONTENT) {}

DeskewParams::DeskewParams(const QDomElement& el)
    : m_deskewAngleDeg(angleFromXml(el, "deskewAngleDeg", MAX_DESKEW_ANGLE_DEG)),
      m_mode(stringToAutoManualMode(el.attribute("mode"))),
      m_autoOblique(el.attribute("autoOblique", "0") != "0"),
      m_detection(skewDetectionFromString(el.attribute("detection"), DETECT_CONTENT)) {}

QDomElement DeskewParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("deskewAngleDeg", foundation::Utils::doubleToString(m_deskewAngleDeg));
  el.setAttribute("mode", autoManualModeToString(m_mode));
  el.setAttribute("autoOblique", m_autoOblique ? "1" : "0");
  el.setAttribute("detection", skewDetectionToString(m_detection));
  return el;
}

double DeskewParams::getDeskewAngleDeg() const {
  return m_deskewAngleDeg;
}

void DeskewParams::setDeskewAngleDeg(double deskewAngleDeg) {
  m_deskewAngleDeg = deskewAngleDeg;
}

AutoManualMode DeskewParams::getMode() const {
  return m_mode;
}

void DeskewParams::setMode(AutoManualMode mode) {
  m_mode = mode;
}

bool DeskewParams::isAutoOblique() const {
  return m_autoOblique;
}

void DeskewParams::setAutoOblique(bool autoOblique) {
  m_autoOblique = autoOblique;
}

SkewDetection DeskewParams::getDetection() const {
  return m_detection;
}

void DeskewParams::setDetection(SkewDetection detection) {
  m_detection = detection;
}
}  // namespace deskew