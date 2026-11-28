// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_PHOTOADJUSTMENTSCONTROLS_H_
#define SCANTAILOR_OUTPUT_PHOTOADJUSTMENTSCONTROLS_H_

#include <QObject>
#include <array>
#include <functional>

#include "../ColorParams.h"
#include "../PhotoAdjustments.h"

class ConnectionManager;
class QAbstractSpinBox;
class QCheckBox;
class QGridLayout;
class QLabel;
class QSlider;
class QWidget;

namespace output {
/**
 * Builds, displays, and reads the photo adjustment sliders/fields inside OptionsWidget.
 */
class PhotoAdjustmentsControls : public QObject {
  Q_OBJECT
 public:
  enum Row { TEMPERATURE, TINT, EXPOSURE, CONTRAST, HIGHLIGHTS, SHADOWS, WHITES, BLACKS };
  static constexpr int ROW_COUNT = BLACKS + 1;

  PhotoAdjustmentsControls(QWidget* panel,
                           QGridLayout* colorBalanceGrid,
                           QGridLayout* toneGrid,
                           QLabel* colorBalanceHeading,
                           QCheckBox* keepWhiteCB,
                           QObject* parent = nullptr);

  void setupConnections(ConnectionManager& connectionManager,
                        const std::function<void(const PhotoAdjustments&)>& onChanged);

  void updateDisplay(ColorMode colorMode, bool grayscaleOutput, const PhotoAdjustments& adjustments);

  PhotoAdjustments currentValues() const;

 private:
  struct RowWidgets {
    QLabel* label = nullptr;
    QAbstractSpinBox* field = nullptr;  // QDoubleSpinBox for EXPOSURE, QSpinBox otherwise.
    QSlider* slider = nullptr;
  };

  int sliderValueFor(int row, const PhotoAdjustments& adjustments) const;

  void onSliderChanged(int row, int value, const std::function<void(const PhotoAdjustments&)>& onChanged);

  void onFieldChanged(int row, const std::function<void(const PhotoAdjustments&)>& onChanged);

  QWidget* m_panel;
  QLabel* m_colorBalanceHeading;
  QCheckBox* m_keepWhiteCB;
  std::array<RowWidgets, ROW_COUNT> m_rows;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_PHOTOADJUSTMENTSCONTROLS_H_
