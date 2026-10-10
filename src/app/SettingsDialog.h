// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_SETTINGSDIALOG_H_
#define SCANTAILOR_APP_SETTINGSDIALOG_H_

#include <QColor>
#include <QDialog>

#include "ui_SettingsDialog.h"

class SettingsDialog : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

  ~SettingsDialog() override;

 signals:
  void settingsChanged();

 private slots:
  void commitChanges();

  void blackOnWhiteDetectionToggled(bool checked);

 private:
  void chooseAccentColor();

  /** The chosen accent color, or the default of the selected color scheme. */
  QColor displayedAccentColor() const;

  void updateAccentColorDisplay();

  void showRestartNotice();

  Ui::SettingsDialog ui;
  /** The chosen accent color, or an invalid color for the default of the color scheme. */
  QColor m_accentColor;
};


#endif
