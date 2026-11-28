// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PhotoAdjustmentsControls.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QtGlobal>

#include "ConnectionManager.h"
#include "WidgetUtils.h"

namespace output {
namespace {
QString translateOptions(const char* text) {
  return QCoreApplication::translate("output::OptionsWidget", text);
}
}  // namespace

PhotoAdjustmentsControls::PhotoAdjustmentsControls(QWidget* panel,
                                                   QGridLayout* colorBalanceGrid,
                                                   QGridLayout* toneGrid,
                                                   QLabel* colorBalanceHeading,
                                                   QCheckBox* keepWhiteCB,
                                                   QObject* parent)
    : QObject(parent), m_panel(panel), m_colorBalanceHeading(colorBalanceHeading), m_keepWhiteCB(keepWhiteCB) {
  struct RowText {
    const char* label;
    const char* toolTip;
  };
  static constexpr RowText kTexts[ROW_COUNT]
      = {{QT_TRANSLATE_NOOP("output::OptionsWidget", "Temperature:"),
          QT_TRANSLATE_NOOP(
              "output::OptionsWidget",
              "Positive values make the pictures warmer (more yellow), negative values cooler (more blue).")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Tint:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget", "Positive values add magenta, negative values green.")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Exposure:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget", "Brightens or darkens the pictures as a whole, in stops (EV).")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Contrast:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget",
                            "Positive values spread the tones apart, negative values bring them closer together, "
                            "around the middle gray.")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Highlights:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget",
                            "Positive values brighten the bright parts, negative values darken them.")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Shadows:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget",
                            "Positive values brighten the dark parts, negative values darken them.")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Whites:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget",
                            "Positive values make the brightest tones whiter, negative values duller.")},
         {QT_TRANSLATE_NOOP("output::OptionsWidget", "Blacks:"),
          QT_TRANSLATE_NOOP("output::OptionsWidget",
                            "Positive values make the darkest tones lighter, negative values deeper.")}};

  for (int row = 0; row < ROW_COUNT; ++row) {
    RowWidgets& r = m_rows[row];
    r.label = new QLabel(translateOptions(kTexts[row].label), m_panel);
    r.slider = new QSlider(Qt::Horizontal, m_panel);
    if (row == EXPOSURE) {
      // The slider counts hundredths of a stop.
      auto* field = new QDoubleSpinBox(m_panel);
      field->setRange(PhotoAdjustments::MIN_EXPOSURE, PhotoAdjustments::MAX_EXPOSURE);
      field->setDecimals(2);
      field->setSingleStep(0.05);
      field->setSuffix(QStringLiteral(" EV"));
      r.field = field;
      r.slider->setRange(qRound(PhotoAdjustments::MIN_EXPOSURE * 100), qRound(PhotoAdjustments::MAX_EXPOSURE * 100));
      r.slider->setSingleStep(5);
      r.slider->setPageStep(25);
    } else {
      auto* field = new QSpinBox(m_panel);
      field->setRange(PhotoAdjustments::MIN_VALUE, PhotoAdjustments::MAX_VALUE);
      r.field = field;
      r.slider->setRange(PhotoAdjustments::MIN_VALUE, PhotoAdjustments::MAX_VALUE);
      r.slider->setSingleStep(1);
      r.slider->setPageStep(10);
    }
    r.field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.field->setKeyboardTracking(false);
    const QString toolTip = translateOptions(kTexts[row].toolTip);
    for (QWidget* widget :
         {static_cast<QWidget*>(r.label), static_cast<QWidget*>(r.field), static_cast<QWidget*>(r.slider)}) {
      widget->setToolTip(toolTip);
    }

    QGridLayout* grid = (row <= TINT) ? colorBalanceGrid : toneGrid;
    const int gridRow = 2 * ((row <= TINT) ? row : row - EXPOSURE);
    grid->addWidget(r.label, gridRow, 0);
    grid->addWidget(r.field, gridRow, 1, Qt::AlignLeft);
    grid->addWidget(r.slider, gridRow + 1, 0, 1, 2);
  }
  // Equal fields, as wide as "-2.00 EV".
  QAbstractSpinBox* exposureField = m_rows[EXPOSURE].field;
  core::WidgetUtils::setNumberFieldWidth(exposureField, PhotoAdjustments::MIN_EXPOSURE);
  for (RowWidgets& r : m_rows) {
    r.field->setFixedWidth(exposureField->minimumWidth());
  }
  // The fields of both parts start at the same place.
  core::WidgetUtils::alignLabelColumns({colorBalanceGrid, toneGrid});
  for (QGridLayout* grid : {colorBalanceGrid, toneGrid}) {
    grid->setColumnStretch(1, 1);
  }
}

void PhotoAdjustmentsControls::setupConnections(ConnectionManager& connectionManager,
                                                const std::function<void(const PhotoAdjustments&)>& onChanged) {
  for (int row = 0; row < ROW_COUNT; ++row) {
    const RowWidgets& r = m_rows[row];
    connectionManager.addConnection(
        connect(r.slider, &QAbstractSlider::valueChanged, this,
                [this, row, onChanged](int value) { onSliderChanged(row, value, onChanged); }));
    connectionManager.addConnection(
        connect(r.slider, &QAbstractSlider::sliderReleased, this, [this, onChanged] { onChanged(currentValues()); }));
    if (auto* doubleField = qobject_cast<QDoubleSpinBox*>(r.field)) {
      connectionManager.addConnection(connect(doubleField, &QDoubleSpinBox::valueChanged, this,
                                              [this, row, onChanged] { onFieldChanged(row, onChanged); }));
    } else if (auto* intField = qobject_cast<QSpinBox*>(r.field)) {
      connectionManager.addConnection(
          connect(intField, &QSpinBox::valueChanged, this, [this, row, onChanged] { onFieldChanged(row, onChanged); }));
    }
  }
  connectionManager.addConnection(
      connect(m_keepWhiteCB, &QAbstractButton::clicked, this, [this, onChanged] { onChanged(currentValues()); }));
}

int PhotoAdjustmentsControls::sliderValueFor(const int row, const PhotoAdjustments& adjustments) const {
  switch (row) {
    case TEMPERATURE:
      return adjustments.temperature();
    case TINT:
      return adjustments.tint();
    case EXPOSURE:
      return qRound(adjustments.exposure() * 100);
    case CONTRAST:
      return adjustments.contrast();
    case HIGHLIGHTS:
      return adjustments.highlights();
    case SHADOWS:
      return adjustments.shadows();
    case WHITES:
      return adjustments.whites();
    case BLACKS:
      return adjustments.blacks();
    default:
      return 0;
  }
}

void PhotoAdjustmentsControls::onSliderChanged(const int row,
                                               const int value,
                                               const std::function<void(const PhotoAdjustments&)>& onChanged) {
  RowWidgets& r = m_rows[row];
  {
    const QSignalBlocker blocker(r.field);
    if (auto* doubleField = qobject_cast<QDoubleSpinBox*>(r.field)) {
      doubleField->setValue(value / 100.0);
    } else if (auto* intField = qobject_cast<QSpinBox*>(r.field)) {
      intField->setValue(value);
    }
  }
  // While dragging, the page is recalculated only once the slider is released.
  if (!r.slider->isSliderDown()) {
    onChanged(currentValues());
  }
}

void PhotoAdjustmentsControls::onFieldChanged(const int row,
                                              const std::function<void(const PhotoAdjustments&)>& onChanged) {
  RowWidgets& r = m_rows[row];
  {
    const QSignalBlocker blocker(r.slider);
    if (auto* doubleField = qobject_cast<QDoubleSpinBox*>(r.field)) {
      r.slider->setValue(qRound(doubleField->value() * 100));
    } else if (auto* intField = qobject_cast<QSpinBox*>(r.field)) {
      r.slider->setValue(intField->value());
    }
  }
  onChanged(currentValues());
}

PhotoAdjustments PhotoAdjustmentsControls::currentValues() const {
  const auto value = [this](const int row) { return m_rows[row].slider->value(); };
  PhotoAdjustments adjustments;
  adjustments.setTemperature(value(TEMPERATURE));
  adjustments.setTint(value(TINT));
  adjustments.setExposure(value(EXPOSURE) / 100.0);
  adjustments.setContrast(value(CONTRAST));
  adjustments.setHighlights(value(HIGHLIGHTS));
  adjustments.setShadows(value(SHADOWS));
  adjustments.setWhites(value(WHITES));
  adjustments.setBlacks(value(BLACKS));
  adjustments.setKeepWhite(m_keepWhiteCB->isChecked());
  return adjustments;
}

void PhotoAdjustmentsControls::updateDisplay(const ColorMode colorMode,
                                             const bool grayscaleOutput,
                                             const PhotoAdjustments& adjustments) {
  for (int row = 0; row < ROW_COUNT; ++row) {
    RowWidgets& r = m_rows[row];
    const QSignalBlocker sliderBlocker(r.slider);
    const QSignalBlocker fieldBlocker(r.field);
    const int value = sliderValueFor(row, adjustments);
    r.slider->setValue(value);
    if (auto* doubleField = qobject_cast<QDoubleSpinBox*>(r.field)) {
      doubleField->setValue(value / 100.0);
    } else if (auto* intField = qobject_cast<QSpinBox*>(r.field)) {
      intField->setValue(value);
    }
  }
  const QSignalBlocker keepWhiteBlocker(m_keepWhiteCB);
  m_keepWhiteCB->setChecked(adjustments.keepWhite());

  // Not used in black and white; the color balance not with grayscale output, "Keep white" only
  // in color mode (in mixed mode only the pictures are changed).
  const bool colorBalance = !grayscaleOutput;
  m_panel->setEnabled(colorMode != BLACK_AND_WHITE);
  m_colorBalanceHeading->setEnabled(colorBalance);
  for (const int row : {TEMPERATURE, TINT}) {
    m_rows[row].label->setEnabled(colorBalance);
    m_rows[row].field->setEnabled(colorBalance);
    m_rows[row].slider->setEnabled(colorBalance);
  }
  m_keepWhiteCB->setEnabled(colorMode == COLOR_GRAYSCALE);
}
}  // namespace output
