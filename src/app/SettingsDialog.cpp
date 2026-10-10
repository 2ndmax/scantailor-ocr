// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SettingsDialog.h"

#include <core/ApplicationSettings.h>
#include <core/DarkColorScheme.h>
#include <core/LightColorScheme.h>

#include <QColorDialog>
#include <QPainter>
#include <QtCore/QDir>
#include <QtWidgets/QMessageBox>
#include <cmath>

#include "Application.h"
#include "OpenGLSupport.h"

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
  ui.setupUi(this);

  ApplicationSettings& settings = ApplicationSettings::getInstance();

  if (!OpenGLSupport::supported()) {
    ui.enableOpenglCb->setChecked(false);
    ui.enableOpenglCb->setEnabled(false);
    ui.openglDeviceLabel->setEnabled(false);
    ui.openglDeviceLabel->setText(tr("Your hardware / driver don't provide the necessary features"));
  } else {
    ui.enableOpenglCb->setChecked(settings.isOpenGlEnabled());
    const QString openglDevicePattern = ui.openglDeviceLabel->text();
    ui.openglDeviceLabel->setText(openglDevicePattern.arg(OpenGLSupport::deviceName()));
  }

  ui.colorSchemeBox->addItem(tr("Dark"), "dark");
  ui.colorSchemeBox->addItem(tr("Light"), "light");
  ui.colorSchemeBox->addItem(tr("Native"), "native");
  ui.colorSchemeBox->setCurrentIndex(ui.colorSchemeBox->findData(settings.getColorScheme()));
  connect(ui.colorSchemeBox, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), [this](int) {
    updateAccentColorDisplay();
    showRestartNotice();
  });

  m_accentColor = settings.getAccentColor();
  connect(ui.accentColorButton, &QPushButton::clicked, this, &SettingsDialog::chooseAccentColor);
  connect(ui.accentColorDefaultButton, &QPushButton::clicked, this, [this]() {
    m_accentColor = QColor();
    updateAccentColorDisplay();
    showRestartNotice();
  });
  updateAccentColorDisplay();

  {
    auto* app = static_cast<Application*>(qApp);
    for (const QString& locale : app->getLanguagesList()) {
      QString languageName = QLocale::languageToString(QLocale(locale).language());
      ui.languageBox->addItem(languageName, locale);
    }
    ui.languageBox->setCurrentIndex(ui.languageBox->findData(app->getCurrentLocale()));
    ui.languageBox->setEnabled(ui.languageBox->count() > 1);
  }

  ui.blackOnWhiteDetectionCB->setChecked(settings.isBlackOnWhiteDetectionEnabled());
  ui.blackOnWhiteDetectionAtOutputCB->setEnabled(ui.blackOnWhiteDetectionCB->isChecked());
  ui.blackOnWhiteDetectionAtOutputCB->setChecked(settings.isBlackOnWhiteDetectionOutputEnabled());
  connect(ui.blackOnWhiteDetectionCB, SIGNAL(clicked(bool)), SLOT(blackOnWhiteDetectionToggled(bool)));

  ui.highlightDeviationCB->setChecked(settings.isHighlightDeviationEnabled());

  ui.deskewDeviationCoefSB->setValue(settings.getDeskewDeviationCoef());
  ui.deskewDeviationThresholdSB->setValue(settings.getDeskewDeviationThreshold());
  ui.selectContentDeviationCoefSB->setValue(settings.getSelectContentDeviationCoef());
  ui.selectContentDeviationThresholdSB->setValue(settings.getSelectContentDeviationThreshold());
  ui.marginsDeviationCoefSB->setValue(settings.getMarginsDeviationCoef());
  ui.marginsDeviationThresholdSB->setValue(settings.getMarginsDeviationThreshold());

  ui.autoSaveProjectCB->setChecked(settings.isAutoSaveProjectEnabled());

  ui.thumbnailQualitySB->setValue(settings.getThumbnailQuality().width());
  ui.thumbnailSizeSB->setValue(settings.getMaxLogicalThumbnailSize().toSize().width());

  ui.singleColumnThumbnailsCB->setChecked(settings.isSingleColumnThumbnailDisplayEnabled());
  ui.cancelingSelectionQuestionCB->setChecked(settings.isCancelingSelectionQuestionEnabled());

  ui.deskewHandleDistanceSB->setRange(ApplicationSettings::MIN_DESKEW_HANDLE_DISTANCE,
                                      ApplicationSettings::MAX_DESKEW_HANDLE_DISTANCE);
  ui.deskewHandleDistanceSB->setValue(settings.getDeskewHandleDistance());

  connect(ui.buttonBox, SIGNAL(accepted()), SLOT(commitChanges()));
}

SettingsDialog::~SettingsDialog() = default;

void SettingsDialog::commitChanges() {
  ApplicationSettings& settings = ApplicationSettings::getInstance();

  settings.setOpenGlEnabled(ui.enableOpenglCb->isChecked());
  settings.setAutoSaveProjectEnabled(ui.autoSaveProjectCB->isChecked());
  settings.setHighlightDeviationEnabled(ui.highlightDeviationCB->isChecked());
  settings.setColorScheme(ui.colorSchemeBox->currentData().toString());
  settings.setAccentColor(m_accentColor);

  settings.setLanguage(ui.languageBox->currentData().toString());

  settings.setDeskewDeviationCoef(ui.deskewDeviationCoefSB->value());
  settings.setDeskewDeviationThreshold(ui.deskewDeviationThresholdSB->value());
  settings.setSelectContentDeviationCoef(ui.selectContentDeviationCoefSB->value());
  settings.setSelectContentDeviationThreshold(ui.selectContentDeviationThresholdSB->value());
  settings.setMarginsDeviationCoef(ui.marginsDeviationCoefSB->value());
  settings.setMarginsDeviationThreshold(ui.marginsDeviationThresholdSB->value());

  settings.setBlackOnWhiteDetectionEnabled(ui.blackOnWhiteDetectionCB->isChecked());
  settings.setBlackOnWhiteDetectionOutputEnabled(ui.blackOnWhiteDetectionAtOutputCB->isChecked());

  {
    const int quality = ui.thumbnailQualitySB->value();
    settings.setThumbnailQuality(QSize(quality, quality));
  }
  {
    const double width = ui.thumbnailSizeSB->value();
    const double height = std::round((width * (16.0 / 25.0)) * 100) / 100;
    settings.setMaxLogicalThumbnailSize(QSizeF(width, height));
  }

  settings.setSingleColumnThumbnailDisplayEnabled(ui.singleColumnThumbnailsCB->isChecked());
  settings.setCancelingSelectionQuestionEnabled(ui.cancelingSelectionQuestionCB->isChecked());
  settings.setDeskewHandleDistance(ui.deskewHandleDistanceSB->value());

  emit settingsChanged();
}

void SettingsDialog::blackOnWhiteDetectionToggled(bool checked) {
  ui.blackOnWhiteDetectionAtOutputCB->setEnabled(checked);
}

void SettingsDialog::chooseAccentColor() {
  const QColor shown = displayedAccentColor();
  const QColor color = QColorDialog::getColor(shown, this, tr("Accent Color"));
  if (!color.isValid() || (color == shown)) {
    return;
  }
  m_accentColor = color;
  updateAccentColorDisplay();
  showRestartNotice();
}

QColor SettingsDialog::displayedAccentColor() const {
  if (m_accentColor.isValid()) {
    return m_accentColor;
  }
  return QColor((ui.colorSchemeBox->currentData().toString() == "light") ? LightColorScheme::DEFAULT_ACCENT_COLOR
                                                                         : DarkColorScheme::DEFAULT_ACCENT_COLOR);
}

void SettingsDialog::updateAccentColorDisplay() {
  // The native color scheme takes its colors from the system.
  const bool native = (ui.colorSchemeBox->currentData().toString() == "native");
  const QColor shown = displayedAccentColor();

  QPixmap swatch(16, 16);
  swatch.fill(shown);
  {
    QPainter painter(&swatch);
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawRect(swatch.rect().adjusted(0, 0, -1, -1));
  }
  ui.accentColorButton->setIcon(QIcon(swatch));
  ui.accentColorButton->setText(shown.name());
  ui.accentColorLabel->setEnabled(!native);
  ui.accentColorButton->setEnabled(!native);
  ui.accentColorDefaultButton->setEnabled(!native && m_accentColor.isValid());
}

void SettingsDialog::showRestartNotice() {
  QMessageBox::information(this, tr("Information"),
                           tr("ScanTailor OCR needs to be restarted to apply the color scheme changes."));
}
