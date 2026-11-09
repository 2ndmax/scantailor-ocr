// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BlackWhiteOptions.h"

#include <foundation/Utils.h>

#include <QDomDocument>
#include <algorithm>
#include <cmath>

#include "XmlUnmarshaller.h"

using namespace foundation;

namespace output {
namespace {
int intFromXml(const QDomElement& el, const QString& name, const int fallback) {
  bool ok = false;
  const int value = el.attribute(name).toInt(&ok);
  return ok ? value : fallback;
}

// A window size below the minimum (also a missing one) can't come from the program, and most
// binarization methods refuse one of 0 or less, which fails the page.
int windowSizeFromXml(const QDomElement& el, const QString& name, const int fallback) {
  const int size = intFromXml(el, name, fallback);
  return (size >= BlackWhiteOptions::MIN_WINDOW_SIZE) ? size : fallback;
}

// Far beyond what the fields allow (delta up to 100, strength up to 9.99), so that no file made by a
// program loses its value. A larger one overflows the threshold or doesn't fit a float.
constexpr double LARGEST_PLAUSIBLE_DELTA_OR_STRENGTH = 1000.0;

int deltaFromXml(const QDomElement& el, const QString& name, const int fallback) {
  const int delta = intFromXml(el, name, fallback);
  return (std::abs(static_cast<double>(delta)) <= LARGEST_PLAUSIBLE_DELTA_OR_STRENGTH) ? delta : fallback;
}

double strengthFromXml(const QDomElement& el, const QString& name, const double fallback) {
  const double strength = XmlUnmarshaller::number(el, name, fallback);
  return (std::abs(strength) <= LARGEST_PLAUSIBLE_DELTA_OR_STRENGTH) ? strength : fallback;
}

// Files of ScanTailor Advanced have two strengths: one for these methods, one for all others.
bool hasStrengthInWolfCoefAttribute(const BinarizationMethod method) {
  return (method == T_WOLF) || (method == T_FOX) || (method == T_WINDOW) || (method == T_GRAD);
}

// Files of ScanTailor Advanced hold one set of values as attributes of the element. They count for
// the method set for the page.
BlackWhiteOptions::MethodValues valuesFromAttributes(const QDomElement& el, const BinarizationMethod method) {
  const BlackWhiteOptions::MethodValues defaults = BlackWhiteOptions::defaultValues(method);
  return {deltaFromXml(el, "thresholdAdj", defaults.thresholdAdjustment),
          strengthFromXml(el, hasStrengthInWolfCoefAttribute(method) ? "wolfCoef" : "sauvolaCoef", defaults.coef),
          windowSizeFromXml(el, "windowSize", defaults.windowSize)};
}

BlackWhiteOptions::MethodValues valuesFromMethodElement(const QDomElement& el, const BinarizationMethod method) {
  const BlackWhiteOptions::MethodValues defaults = BlackWhiteOptions::defaultValues(method);
  return {deltaFromXml(el, "delta", defaults.thresholdAdjustment), strengthFromXml(el, "coef", defaults.coef),
          windowSizeFromXml(el, "window", defaults.windowSize)};
}
}  // namespace

BlackWhiteOptions::MethodValues BlackWhiteOptions::defaultValues(const BinarizationMethod method) {
  MethodValues values{0, 0.34, DEFAULT_WINDOW_SIZE};
  // Without a default, so that the compiler asks about a method added later.
  switch (method) {
    case T_OTSU:
    case T_SAUVOLA:
    case T_BRADLEY:
    case T_EDGEPLUS:
    case T_BLURDIV:
    case T_EDGEDIV:
      break;
    case T_WOLF:
    case T_FOX:
    case T_WINDOW:
    case T_GRAD:
      values.coef = 0.3;
      break;
    // The methods below as set by ScanTailor Experimental when choosing them, with its radius doubled.
    case T_GATOS:
      values.coef = 0.2;
      break;
    case T_NICK:
      values.coef = 0.1;
      break;
    case T_SINGH:
      values.coef = 0.3;
      break;
    case T_ROBUST:
    case T_MULTISCALE:
      values.coef = 0.2;
      values.windowSize = 40;
      break;
  }
  return values;
}

BlackWhiteOptions::BlackWhiteOptions()
    : m_savitzkyGolaySmoothingEnabled(true),
      m_morphologicalSmoothingEnabled(true),
      m_normalizeIllumination(true),
      m_lowerBound(1),
      m_upperBound(254),
      m_coloredDimming(0.0),
      m_binarizationMethod(T_OTSU) {
  setAllValuesToDefaults();
}

BlackWhiteOptions::BlackWhiteOptions(const QDomElement& el)
    : m_savitzkyGolaySmoothingEnabled(el.attribute("savitzkyGolaySmoothing") == "1"),
      m_morphologicalSmoothingEnabled(el.attribute("morphologicalSmoothing") == "1"),
      m_normalizeIllumination(el.attribute("normalizeIlluminationBW") == "1"),
      // Files of ScanTailor Advanced name them after the method that used them first.
      m_lowerBound(intFromXml(el, "lowerBound", intFromXml(el, "wolfLowerBound", 1))),
      m_upperBound(intFromXml(el, "upperBound", intFromXml(el, "wolfUpperBound", 254))),
      m_coloredDimming(XmlUnmarshaller::numberInRange(el, "coloredDimming", MIN_COLORED_DIMMING, MAX_COLORED_DIMMING)),
      m_binarizationMethod(parseBinarizationMethod(el.attribute("binarizationMethod"))),
      m_colorSegmenterOptions(el.namedItem("color-segmenter-options").toElement()) {
  setAllValuesToDefaults();

  QDomElement methodEl = el.firstChildElement("method");
  if (methodEl.isNull()) {
    setValues(m_binarizationMethod, valuesFromAttributes(el, m_binarizationMethod));
  }
  for (; !methodEl.isNull(); methodEl = methodEl.nextSiblingElement("method")) {
    const QString methodName = methodEl.attribute("name");
    const BinarizationMethod method = parseBinarizationMethod(methodName);
    // An unknown name reads as Otsu, whose values it must not replace.
    if ((method != T_OTSU) || (methodName == formatBinarizationMethod(T_OTSU))) {
      setValues(method, valuesFromMethodElement(methodEl, method));
    }
  }
}

void BlackWhiteOptions::setAllValuesToDefaults() {
  for (int i = 0; i < METHOD_COUNT; ++i) {
    m_values[i] = defaultValues(static_cast<BinarizationMethod>(i));
  }
}

QDomElement BlackWhiteOptions::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("savitzkyGolaySmoothing", m_savitzkyGolaySmoothingEnabled ? "1" : "0");
  el.setAttribute("morphologicalSmoothing", m_morphologicalSmoothingEnabled ? "1" : "0");
  el.setAttribute("normalizeIlluminationBW", m_normalizeIllumination ? "1" : "0");
  el.setAttribute("lowerBound", m_lowerBound);
  el.setAttribute("upperBound", m_upperBound);
  if (m_coloredDimming != 0.0) {
    el.setAttribute("coloredDimming", Utils::doubleToString(m_coloredDimming));
  }
  el.setAttribute("binarizationMethod", formatBinarizationMethod(m_binarizationMethod));
  for (int i = 0; i < METHOD_COUNT; ++i) {
    const auto method = static_cast<BinarizationMethod>(i);
    // The method of the page is always written, so that its result doesn't follow a later change of
    // a default. The others only when they differ, as a project has many pages.
    if ((method == m_binarizationMethod) || (m_values[i] != defaultValues(method))) {
      QDomElement methodEl(doc.createElement("method"));
      methodEl.setAttribute("name", formatBinarizationMethod(method));
      methodEl.setAttribute("delta", m_values[i].thresholdAdjustment);
      if (method != T_OTSU) {
        methodEl.setAttribute("coef", Utils::doubleToString(m_values[i].coef));
        methodEl.setAttribute("window", m_values[i].windowSize);
      }
      el.appendChild(methodEl);
    }
  }
  el.appendChild(m_colorSegmenterOptions.toXml(doc, "color-segmenter-options"));
  return el;
}

BlackWhiteOptions::MethodValues BlackWhiteOptions::values(const BinarizationMethod method) const {
  return m_values[method];
}

void BlackWhiteOptions::setValues(const BinarizationMethod method, const MethodValues& values) {
  m_values[method] = values;
}

bool BlackWhiteOptions::MethodValues::operator==(const MethodValues& other) const {
  return (thresholdAdjustment == other.thresholdAdjustment) && (coef == other.coef) && (windowSize == other.windowSize);
}

bool BlackWhiteOptions::MethodValues::operator!=(const MethodValues& other) const {
  return !(*this == other);
}

bool BlackWhiteOptions::operator==(const BlackWhiteOptions& other) const {
  return (m_savitzkyGolaySmoothingEnabled == other.m_savitzkyGolaySmoothingEnabled)
         && (m_morphologicalSmoothingEnabled == other.m_morphologicalSmoothingEnabled)
         && (m_normalizeIllumination == other.m_normalizeIllumination) && (m_values == other.m_values)
         && (m_lowerBound == other.m_lowerBound) && (m_upperBound == other.m_upperBound)
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

namespace {
// Only a damaged or hand-edited file holds a value outside its field. It must not reach the color
// segmenter, which would overflow on it or take a root of a negative number.
int segmenterValue(const QDomElement& el, const char* name, const int min, const int max, const int defaultValue) {
  bool ok = false;
  const int value = el.attribute(name).toInt(&ok);
  return (ok && (value >= min) && (value <= max)) ? value : defaultValue;
}

int thresholdAdjustment(const QDomElement& el, const char* name) {
  return segmenterValue(el, name, -99, 99, 0);
}
}  // namespace

BlackWhiteOptions::ColorSegmenterOptions::ColorSegmenterOptions(const QDomElement& el)
    : m_isEnabled(el.attribute("enabled") == "1"),
      m_noiseReduction(segmenterValue(el, "noiseReduction", 0, 999, 7)),
      m_redThresholdAdjustment(thresholdAdjustment(el, "redThresholdAdjustment")),
      m_greenThresholdAdjustment(thresholdAdjustment(el, "greenThresholdAdjustment")),
      m_blueThresholdAdjustment(thresholdAdjustment(el, "blueThresholdAdjustment")),
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
