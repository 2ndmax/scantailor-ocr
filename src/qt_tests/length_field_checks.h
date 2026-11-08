// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_LENGTH_FIELD_CHECKS_H
#define TST_LENGTH_FIELD_CHECKS_H

#include <QDoubleSpinBox>
#include <QString>
#include <QtGlobal>

#include "Units.h"
#include "UnitsProvider.h"

/** Sets the units of the program for the lifetime of the object. */
class ScopedUnits {
 public:
  explicit ScopedUnits(const Units units) : m_previous(UnitsProvider::getInstance().getUnits()) {
    UnitsProvider::getInstance().setUnits(units);
  }

  ~ScopedUnits() { UnitsProvider::getInstance().setUnits(m_previous); }

  ScopedUnits(const ScopedUnits&) = delete;
  ScopedUnits& operator=(const ScopedUnits&) = delete;

 private:
  Units m_previous;
};

/** Whether a length field shows these decimals, this step and the sign of \p units. */
inline bool lengthFieldShows(const QDoubleSpinBox* field, const Units units, const int decimals, const double step) {
  return (field != nullptr) && (field->decimals() == decimals) && qFuzzyCompare(field->singleStep(), step)
         && (field->suffix() == QChar(' ') + unitsToLocalizedString(units));
}

#endif  // TST_LENGTH_FIELD_CHECKS_H
