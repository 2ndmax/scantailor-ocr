// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "UnitsConverter.h"

#include "Dpm.h"

UnitsConverter::UnitsConverter(const Dpi& dpi) : m_dpi(dpi) {}

void UnitsConverter::convert(double& horizontalValue, double& verticalValue, Units fromUnits, Units toUnits) const {
  if (fromUnits == toUnits) {
    return;
  }
  // Only pixels need the resolution; millimetres, centimetres and inches convert without it.
  if (m_dpi.isNull() && ((fromUnits == PIXELS) || (toUnits == PIXELS))) {
    return;
  }

  auto dpm = Dpm(m_dpi);
  switch (fromUnits) {
    case PIXELS:
      switch (toUnits) {
        case MILLIMETRES:
          horizontalValue = horizontalValue / dpm.horizontal() * 1000.;
          verticalValue = verticalValue / dpm.vertical() * 1000.;
          break;
        case CENTIMETRES:
          horizontalValue = horizontalValue / dpm.horizontal() * 100.;
          verticalValue = verticalValue / dpm.vertical() * 100.;
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
          horizontalValue = horizontalValue / 1000. * dpm.horizontal();
          verticalValue = verticalValue / 1000. * dpm.vertical();
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
          horizontalValue = horizontalValue / 100. * dpm.horizontal();
          verticalValue = verticalValue / 100. * dpm.vertical();
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
