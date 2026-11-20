// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "UnitsConverter.h"

UnitsConverter::UnitsConverter(const Dpi& dpi) : m_dpi(dpi) {}

void UnitsConverter::convert(double& horizontalValue, double& verticalValue, Units fromUnits, Units toUnits) const {
  if (fromUnits == toUnits) {
    return;
  }
  // Only pixels need the resolution; millimetres, centimetres and inches convert without it.
  // REVIEW-NOTE (2026-10): without a DPI, conversions from or to pixels return the value unchanged
  // -> callers store pixels as millimeters. Not fixed because projects cannot be created with pages
  // lacking a DPI; only hand-edited project files reach this.
  if (m_dpi.isNull() && ((fromUnits == PIXELS) || (toUnits == PIXELS))) {
    return;
  }

  // Calculated with the exact resolution: Dpm would round 72 dpi to whole pixels per metre,
  // and pixels converted to millimetres and to inches would no longer agree.
  switch (fromUnits) {
    case PIXELS:
      switch (toUnits) {
        case MILLIMETRES:
          horizontalValue = horizontalValue / m_dpi.horizontal() * 25.4;
          verticalValue = verticalValue / m_dpi.vertical() * 25.4;
          break;
        case CENTIMETRES:
          horizontalValue = horizontalValue / m_dpi.horizontal() * 2.54;
          verticalValue = verticalValue / m_dpi.vertical() * 2.54;
          break;
        case INCHES:
          horizontalValue /= m_dpi.horizontal();
          verticalValue /= m_dpi.vertical();
          break;
        default:
          break;
      }
      break;
    case MILLIMETRES:
      switch (toUnits) {
        case PIXELS:
          horizontalValue = horizontalValue / 25.4 * m_dpi.horizontal();
          verticalValue = verticalValue / 25.4 * m_dpi.vertical();
          break;
        case CENTIMETRES:
          horizontalValue = horizontalValue / 10.;
          verticalValue = verticalValue / 10.;
          break;
        case INCHES:
          horizontalValue = horizontalValue / 25.4;
          verticalValue = verticalValue / 25.4;
          break;
        default:
          break;
      }
      break;
    case CENTIMETRES:
      switch (toUnits) {
        case PIXELS:
          horizontalValue = horizontalValue / 2.54 * m_dpi.horizontal();
          verticalValue = verticalValue / 2.54 * m_dpi.vertical();
          break;
        case MILLIMETRES:
          horizontalValue = horizontalValue * 10.;
          verticalValue = verticalValue * 10.;
          break;
        case INCHES:
          horizontalValue = horizontalValue / 2.54;
          verticalValue = verticalValue / 2.54;
          break;
        default:
          break;
      }
      break;
    case INCHES:
      switch (toUnits) {
        case PIXELS:
          horizontalValue *= m_dpi.horizontal();
          verticalValue *= m_dpi.vertical();
          break;
        case MILLIMETRES:
          horizontalValue = horizontalValue * 25.4;
          verticalValue = verticalValue * 25.4;
          break;
        case CENTIMETRES:
          horizontalValue = horizontalValue * 2.54;
          verticalValue = verticalValue * 2.54;
          break;
        default:
          break;
      }
      break;
  }
}

QTransform UnitsConverter::transform(Units fromUnits, Units toUnits) const {
  double xScaleFactor = 1.0;
  double yScaleFactor = 1.0;
  convert(xScaleFactor, yScaleFactor, fromUnits, toUnits);
  return QTransform().scale(xScaleFactor, yScaleFactor);
}
