// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.
// Split Params: Zvezdochiot; dev branch implements as requested (see Params.h).

#include "Params.h"

#include <foundation/Utils.h>

#include <QDomDocument>

using namespace foundation;

namespace deskew {
namespace {

AutoManualMode obliqueModeFromXml(const QDomElement& deskewEl) {
  if (deskewEl.hasAttribute("oblique-mode")) {
    return deskewEl.attribute("oblique-mode") == "manual" ? MODE_MANUAL : MODE_AUTO;
  }
  if (deskewEl.hasAttribute("autoOblique")) {
    return deskewEl.attribute("autoOblique", "1") != "0" ? MODE_AUTO : MODE_MANUAL;
  }
  return deskewEl.attribute("mode") == "manual" ? MODE_MANUAL : MODE_AUTO;
}

}  // namespace

Params::Params(const double deskewAngleDeg,
               const double obliqueDeg,
               const Dependencies& deps,
               const AutoManualMode deskewMode,
               const AutoManualMode obliqueMode,
               const SkewDetection detection)
    : m_rotation{deskewAngleDeg, deskewMode, detection}, m_oblique{obliqueDeg, obliqueMode}, m_deps(deps) {}

Params::Params(const QDomElement& deskewEl, const SkewDetection legacyDetection)
    : m_rotation{deskewEl.attribute("angle").toDouble(),
                 deskewEl.attribute("mode") == "manual" ? MODE_MANUAL : MODE_AUTO,
                 skewDetectionFromString(deskewEl.attribute("detection"), legacyDetection)},
      m_oblique{deskewEl.attribute("oblique").toDouble(), obliqueModeFromXml(deskewEl)},
      m_deps(deskewEl.namedItem("dependencies").toElement()) {}

Params::~Params() = default;

bool Params::needsRemeasure(const Dependencies& deps) const {
  return !deps.matches(m_deps) && ((m_rotation.mode == MODE_AUTO) || (m_oblique.mode == MODE_AUTO));
}

Params Params::mergedForApply(const Params* existing,
                              const Params& current,
                              const bool applyDeskew,
                              const bool applyOblique,
                              const bool isCurrentPage,
                              const SkewDetection defaultDetection) {
  // Dependencies describe the image a page was computed from, so they are never transferable
  // between pages. Keep the ones the target page already has; a page we know nothing about gets
  // empty dependencies, which makes the task recompute (auto mode) or fill them in (manual mode).
  // Automatic angles are measured on each page itself, deskew with the detection method applied,
  // rather than copying the angles of the current page.
  const bool remeasure
      = !isCurrentPage
        && ((applyDeskew && current.mode() == MODE_AUTO) || (applyOblique && current.obliqueMode() == MODE_AUTO));
  const Dependencies deps(existing && !remeasure ? existing->dependencies() : Dependencies());
  if (!existing) {
    return Params(applyDeskew ? current.deskewAngle() : 0.0, applyOblique ? current.obliqueAngle() : 0.0, deps,
                  applyDeskew ? current.mode() : MODE_AUTO, applyOblique ? current.obliqueMode() : MODE_AUTO,
                  applyDeskew ? current.detection() : defaultDetection);
  }
  return Params(applyDeskew ? current.deskewAngle() : existing->deskewAngle(),
                applyOblique ? current.obliqueAngle() : existing->obliqueAngle(), deps,
                applyDeskew ? current.mode() : existing->mode(),
                applyOblique ? current.obliqueMode() : existing->obliqueMode(),
                applyDeskew ? current.detection() : existing->detection());
}

QDomElement Params::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("mode", m_rotation.mode == MODE_AUTO ? "auto" : "manual");
  el.setAttribute("angle", Utils::doubleToString(m_rotation.angle));
  el.setAttribute("detection", skewDetectionToString(m_rotation.detection));
  el.setAttribute("oblique", Utils::doubleToString(m_oblique.obliqueAngle));
  el.setAttribute("oblique-mode", m_oblique.mode == MODE_AUTO ? "auto" : "manual");
  el.appendChild(m_deps.toXml(doc, "dependencies"));
  return el;
}
}  // namespace deskew