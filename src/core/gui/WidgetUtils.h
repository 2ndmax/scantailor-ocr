// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_WIDGETUTILS_H_
#define SCANTAILOR_CORE_WIDGETUTILS_H_

#include <QList>
#include <QObject>

#include "Units.h"

class QAbstractSpinBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QValidator;

namespace core {
/**
 * Helpers for widgets and widget trees. The helpers without widgets are in Utils.
 */
class WidgetUtils {
 public:
  template <typename T>
  static T castOrFindChild(QObject* object);

  /// Accepts up to four digits, optionally followed by "dpi".
  static QValidator* createDpiValidator(QObject* parent);

  /// Makes an editable list of resolutions as wide as three digits, e.g. "600 dpi"; four can still be typed in.
  static void setDpiFieldWidth(QComboBox* field);

  /// Makes a number field as wide as the given value with the field's decimals, prefix and suffix,
  /// e.g. -999.9 for "-999.9 mm", instead of as wide as its range. Fields of the same kind get the
  /// same value and so the same width. With decimals given, the value is written with them instead,
  /// so fields with different decimals can get the same width.
  static void setNumberFieldWidth(QAbstractSpinBox* field, double widestValue, int decimals = -1);

  /// Sets up a spin box for a length in the given units: decimals, step, the unit in the field and,
  /// from \p widestValue, the width. A field in pixels shows whole pixels.
  static void setUpLengthField(QDoubleSpinBox* field, Units units, double widestValue);


  /// Gives the label columns of the grids the same width and the grids the same spacing, so that
  /// the labels and fields of grids shown below each other line up.
  static void alignLabelColumns(const QList<QGridLayout*>& grids, int column = 0);

  WidgetUtils() = delete;
};


template <typename T>
T WidgetUtils::castOrFindChild(QObject* object) {
  if (!object) {
    return nullptr;
  }

  if (auto result = dynamic_cast<T>(object)) {
    return result;
  } else {
    for (QObject* child : object->children()) {
      if ((result = castOrFindChild<T>(child))) {
        return result;
      }
    }
  }
  return nullptr;
}
}  // namespace core

#endif  // ifndef SCANTAILOR_CORE_WIDGETUTILS_H_
