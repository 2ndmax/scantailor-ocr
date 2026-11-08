// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BinarizationMethods.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QLocale>

namespace output {
namespace {
struct MethodInfo {
  BinarizationMethod method;
  const char* name;
  const char* toolTip;
};

// In the order of BinarizationMethod, as the options panels select by index.
const MethodInfo kMethods[] = {
    {T_OTSU, "Otsu",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "One threshold for the whole page. Fast; suited to clean pages with even "
                       "lighting.")},
    {T_SAUVOLA, "Sauvola",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Local threshold from the mean and the contrast around each pixel. A good "
                       "general choice for printed text.")},
    {T_WOLF, "Wolf",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "A variant of Sauvola for low contrast, with a lower and an upper bound.")},
    {T_FOX, "Fox", QT_TRANSLATE_NOOP("BinarizationMethods", "Wolf combined with the contrast measure of Singh.")},
    {T_WINDOW, "Window",
     QT_TRANSLATE_NOOP("BinarizationMethods", "Local threshold with windows that adapt to the content.")},
    {T_BRADLEY, "Bradley",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Compares each pixel with the mean of its surroundings. Simple and "
                       "fast.")},
    {T_GRAD, "Grad", QT_TRANSLATE_NOOP("BinarizationMethods", "Threshold from the gradients of the page.")},
    {T_EDGEPLUS, "EdgePlus",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Strengthens the edges of the letters against their surroundings "
                       "before the threshold.")},
    {T_BLURDIV, "BlurDiv",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Divides by a blurred copy of the page before the threshold, which "
                       "evens out the paper.")},
    {T_EDGEDIV, "EdgeDiv", QT_TRANSLATE_NOOP("BinarizationMethods", "EdgePlus and BlurDiv combined.")},
    {T_GATOS, "Gatos",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Estimates the paper first, then thresholds by the distance to it. For "
                       "yellowed, stained or unevenly lit pages.")},
    {T_NICK, "Nick",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "A variant of Niblack with little noise on empty paper. For faint print on "
                       "old documents.")},
    {T_SINGH, "Singh",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Local threshold from the mean and the local contrast, computed quickly.")},
    {T_ROBUST, "Robust",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Finds dark letters against a brighter surround, hardly affected by "
                       "shadows, for example near the spine.")},
    {T_MULTISCALE, "MultiScale",
     QT_TRANSLATE_NOOP("BinarizationMethods",
                       "Combines thresholds over several window sizes. For pages with "
                       "text of very different sizes.")},
};

bool isFromExperimental(const BinarizationMethod method) {
  return method >= T_GATOS;
}
}  // namespace

void fillBinarizationMethodBox(QComboBox* box) {
  for (const MethodInfo& info : kMethods) {
    box->addItem(QString::fromLatin1(info.name), info.method);
    box->setItemData(box->count() - 1, QCoreApplication::translate("BinarizationMethods", info.toolTip),
                     Qt::ToolTipRole);
  }
}

bool needsOwnBinarizationDefaults(const BinarizationMethod from, const BinarizationMethod to) {
  return (from != to) && (isFromExperimental(from) || isFromExperimental(to));
}

bool usesWolfCoefficient(const BinarizationMethod method) {
  // Without a default, so that the compiler asks about a method added later.
  switch (method) {
    case T_WOLF:
    case T_FOX:
    case T_WINDOW:
    case T_GRAD:
      return true;
    case T_OTSU:
    case T_SAUVOLA:
    case T_BRADLEY:
    case T_EDGEPLUS:
    case T_BLURDIV:
    case T_EDGEDIV:
    case T_GATOS:
    case T_NICK:
    case T_SINGH:
    case T_ROBUST:
    case T_MULTISCALE:
      return false;
  }
  return false;
}

double defaultBinarizationCoef(const BinarizationMethod method) {
  // As set by ScanTailor Experimental when choosing the method.
  switch (method) {
    case T_GATOS:
      return 0.2;
    case T_NICK:
      return 0.1;
    case T_SINGH:
      return 0.3;
    case T_ROBUST:
    case T_MULTISCALE:
      return 0.2;
    default:
      return usesWolfCoefficient(method) ? BlackWhiteOptions().getWolfCoef() : BlackWhiteOptions().getSauvolaCoef();
  }
}

QString binarizationCoefToolTip(const BinarizationMethod method) {
  return QCoreApplication::translate("BinarizationMethods", "Strength of the method. Default value is %1.")
      .arg(QLocale().toString(defaultBinarizationCoef(method)));
}

void applyBinarizationDefaults(BlackWhiteOptions& options) {
  const BinarizationMethod method = options.getBinarizationMethod();
  // As set by ScanTailor Experimental when choosing the method, with its radius doubled.
  options.setWindowSize(((method == T_ROBUST) || (method == T_MULTISCALE)) ? 40 : 200);
  // The Wolf family has a value of its own, which stays.
  options.setSauvolaCoef(usesWolfCoefficient(method) ? BlackWhiteOptions().getSauvolaCoef()
                                                     : defaultBinarizationCoef(method));
}
}  // namespace output
