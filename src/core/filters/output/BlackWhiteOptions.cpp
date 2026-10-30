// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BlackWhiteOptions.h"

#include <foundation/Utils.h>

#include <QDomDocument>
#include <algorithm>
#include <cmath>

using namespace foundation;

namespace output {
namespace {
// A window size below the minimum (also a missing one) can't come from the program, and most
// binarization methods refuse one of 0 or less, which fails the page.
int windowSizeFromXml(const QString& value) {
  bool ok = false;
  const int size = value.toInt(&ok);
  return (ok && (size >= BlackWhiteOptions::MIN_WINDOW_SIZE)) ? size : BlackWhiteOptions::DEFAULT_WINDOW_SIZE;
}
}  // namespace

BlackWhiteOptions::BlackWhiteOptions()
    : m_thresholdAdjustment(0),
      m_savitzkyGolaySmoothingEnabled(true),
      m_morphologicalSmoothingEnabled(true),
      m_normalizeIllumination(true),
      m_windowSize(DEFAULT_WINDOW_SIZE),
      m_sauvolaCoef(0.34),
      m_wolfLowerBound(1),
      m_wolfUpperBound(254),
      m_wolfCoef(0.3),
      m_coloredDimming(0.0),
      m_binarizationMethod(T_OTSU) {}

BlackWhiteOptions::BlackWhiteOptions(const QDomElement& el)
    : m_thresholdAdjustment(el.attribute("thresholdAdj").toInt()),
      m_savitzkyGolaySmoothingEnabled(el.attribute("savitzkyGolaySmoothing") == "1"),
      m_morphologicalSmoothingEnabled(el.attribute("morphologicalSmoothing") == "1"),
      m_normalizeIllumination(el.attribute("normalizeIlluminationBW") == "1"),
      m_windowSize(windowSizeFromXml(el.attribute("windowSize"))),
      m_sauvolaCoef(el.attribute("sauvolaCoef", "0.34").toDouble()),
      m_wolfLowerBound(el.attribute("wolfLowerBound", "1").toInt()),
      m_wolfUpperBound(el.attribute("wolfUpperBound", "254").toInt()),
      m_wolfCoef(el.attribute("wolfCoef", "0.3").toDouble()),
      m_coloredDimming(std::clamp(el.attribute("coloredDimming").toDouble(), MIN_COLORED_DIMMING, MAX_COLORED_DIMMING)),
      m_binarizationMethod(parseBinarizationMethod(el.attribute("binarizationMethod"))),
      m_colorSegmenterOptions(el.namedItem("color-segmenter-options").toElement()) {}

QDomElement BlackWhiteOptions::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("thresholdAdj", m_thresholdAdjustment);
  el.setAttribute("savitzkyGolaySmoothing", m_savitzkyGolaySmoothingEnabled ? "1" : "0");
  el.setAttribute("morphologicalSmoothing", m_morphologicalSmoothingEnabled ? "1" : "0");
  el.setAttribute("normalizeIlluminationBW", m_normalizeIllumination ? "1" : "0");
  el.setAttribute("windowSize", m_windowSize);
  el.setAttribute("sauvolaCoef", Utils::doubleToString(m_sauvolaCoef));
  el.setAttribute("wolfLowerBound", m_wolfLowerBound);
  el.setAttribute("wolfUpperBound", m_wolfUpperBound);
  el.setAttribute("wolfCoef", Utils::doubleToString(m_wolfCoef));
  if (m_coloredDimming != 0.0) {
    el.setAttribute("coloredDimming", Utils::doubleToString(m_coloredDimming));
  }
  el.setAttribute("binarizationMethod", formatBinarizationMethod(m_binarizationMethod));
  el.appendChild(m_colorSegmenterOptions.toXml(doc, "color-segmenter-options"));
  return el;
}

bool BlackWhiteOptions::operator==(const BlackWhiteOptions& other) const {
  return (m_thresholdAdjustment == other.m_thresholdAdjustment)
         && (m_savitzkyGolaySmoothingEnabled == other.m_savitzkyGolaySmoothingEnabled)
         && (m_morphologicalSmoothingEnabled == other.m_morphologicalSmoothingEnabled)
         && (m_normalizeIllumination == other.m_normalizeIllumination) && (m_windowSize == other.m_windowSize)
         && (m_sauvolaCoef == other.m_sauvolaCoef) && (m_wolfLowerBound == other.m_wolfLowerBound)
         && (m_wolfUpperBound == other.m_wolfUpperBound) && (m_wolfCoef == other.m_wolfCoef)
         && (m_coloredDimming == other.m_coloredDimming) && (m_binarizationMethod == other.m_binarizationMethod)
         && (m_colorSegmenterOptions == other.m_colorSegmenterOptions);
}

bool BlackWhiteOptions::operator!=(const BlackWhiteOptions& other) const {
  return !(*this == other);
}

BinarizationMethod BlackWhiteOptions::parseBinarizationMethod(const QString& str) {
  if (str == "sauvola") {
    return T_SAUVOLA;
  } else if (str == "wolf") {
    return T_WOLF;
  } else if (str == "fox") {
    return T_FOX;
  } else if (str == "window") {
    return T_WINDOW;
  } else if (str == "bradley") {
    return T_BRADLEY;
  } else if (str == "grad") {
    return T_GRAD;
  } else if (str == "edgeplus") {
    return T_EDGEPLUS;
  } else if (str == "blurdiv") {
    return T_BLURDIV;
  } else if (str == "edgediv") {
    return T_EDGEDIV;
  } else if (str == "gatos") {
    return T_GATOS;
  } else if (str == "nick") {
    return T_NICK;
  } else if (str == "singh") {
    return T_SINGH;
  } else if (str == "robust") {
    return T_ROBUST;
  } else if (str == "multiscale") {
    return T_MULTISCALE;
  } else {
    return T_OTSU;
  }
}

QString BlackWhiteOptions::formatBinarizationMethod(BinarizationMethod type) {
  QString str = "";
  switch (type) {
    case T_OTSU:
      str = "otsu";
      break;
    case T_SAUVOLA:
      str = "sauvola";
      break;
    case T_WOLF:
      str = "wolf";
      break;
    case T_FOX:
      str = "fox";
      break;
    case T_WINDOW:
      str = "window";
      break;
    case T_BRADLEY:
      str = "bradley";
      break;
    case T_GRAD:
      str = "grad";
      break;
    case T_EDGEPLUS:
      str = "edgeplus";
      break;
    case T_BLURDIV:
      str = "blurdiv";
      break;
    case T_EDGEDIV:
      str = "edgediv";
      break;
    case T_GATOS:
      str = "gatos";
      break;
    case T_NICK:
      str = "nick";
      break;
    case T_SINGH:
      str = "singh";
      break;
    case T_ROBUST:
      str = "robust";
      break;
    case T_MULTISCALE:
      str = "multiscale";
      break;
  }
  return str;
}

/*=============================== BlackWhiteOptions::ColorSegmenterOptions ==================================*/

BlackWhiteOptions::ColorSegmenterOptions::ColorSegmenterOptions()
    : m_isEnabled(false),
      m_noiseReduction(7),
      m_redThresholdAdjustment(0),
      m_greenThresholdAdjustment(0),
      m_blueThresholdAdjustment(0),
      m_posterizeEnabled(false),
      m_kmeansEnabled(false),
      m_kmeansColors(6),
      m_kmeansBlackFixed(true) {}

BlackWhiteOptions::ColorSegmenterOptions::ColorSegmenterOptions(const QDomElement& el)
    : m_isEnabled(el.attribute("enabled") == "1"),
      m_noiseReduction(el.attribute("noiseReduction", "7").toInt()),
      m_redThresholdAdjustment(el.attribute("redThresholdAdjustment").toInt()),
      m_greenThresholdAdjustment(el.attribute("greenThresholdAdjustment").toInt()),
      m_blueThresholdAdjustment(el.attribute("blueThresholdAdjustment").toInt()),
      m_posterizeEnabled(el.attribute("posterize") == "1"),
      m_kmeansEnabled(el.attribute("kmeans") == "1"),
      m_kmeansColors(std::clamp(el.attribute("kmeansColors", "6").toInt(), MIN_KMEANS_COLORS, MAX_KMEANS_COLORS)),
      m_kmeansBlackFixed(el.attribute("kmeansBlackFixed", "1") == "1") {}

QDomElement BlackWhiteOptions::ColorSegmenterOptions::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("enabled", m_isEnabled ? "1" : "0");
  el.setAttribute("noiseReduction", m_noiseReduction);
  el.setAttribute("redThresholdAdjustment", m_redThresholdAdjustment);
  el.setAttribute("greenThresholdAdjustment", m_greenThresholdAdjustment);
  el.setAttribute("blueThresholdAdjustment", m_blueThresholdAdjustment);
  // Always written: without it, a project is from before it was kept apart (see ColorParams).
  el.setAttribute("posterize", m_posterizeEnabled ? "1" : "0");
  // Only when not the default, so that older versions read the same file.
  if (m_kmeansEnabled) {
    el.setAttribute("kmeans", "1");
  }
  if (m_kmeansColors != 6) {
    el.setAttribute("kmeansColors", m_kmeansColors);
  }
  if (!m_kmeansBlackFixed) {
    el.setAttribute("kmeansBlackFixed", "0");
  }
  return el;
}

bool BlackWhiteOptions::ColorSegmenterOptions::operator==(const BlackWhiteOptions::ColorSegmenterOptions& other) const {
  return (m_isEnabled == other.m_isEnabled) && (m_noiseReduction == other.m_noiseReduction)
         && (m_redThresholdAdjustment == other.m_redThresholdAdjustment)
         && (m_greenThresholdAdjustment == other.m_greenThresholdAdjustment)
         && (m_blueThresholdAdjustment == other.m_blueThresholdAdjustment)
         && (m_posterizeEnabled == other.m_posterizeEnabled) && (m_kmeansEnabled == other.m_kmeansEnabled)
         && (m_kmeansColors == other.m_kmeansColors) && (m_kmeansBlackFixed == other.m_kmeansBlackFixed);
}

bool BlackWhiteOptions::ColorSegmenterOptions::operator!=(const BlackWhiteOptions::ColorSegmenterOptions& other) const {
  return !(*this == other);
}
}  // namespace output
