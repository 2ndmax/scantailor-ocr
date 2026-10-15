// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_DEFAULTPARAMSDIALOG_H_
#define SCANTAILOR_APP_DEFAULTPARAMSDIALOG_H_

#include <core/ConnectionManager.h>

#include <QButtonGroup>
#include <QWidget>
#include <list>
#include <set>
#include <unordered_map>

#include "DefaultParams.h"
#include "DefaultParamsProfileManager.h"
#include "OrthogonalRotation.h"
#include "ui_DefaultParamsDialog.h"

class DefaultParamsDialog : public QDialog, private Ui::DefaultParamsDialog {
  Q_OBJECT
 public:
  explicit DefaultParamsDialog(QWidget* parent = nullptr);

  ~DefaultParamsDialog() override = default;

 protected:
  void showEvent(QShowEvent* event) override;

  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:

  void rotateLeft();

  void rotateRight();

  void layoutModeToggled(bool manual);

  void deskewModeChanged(bool autoMode);

  void pageDetectAutoToggled(bool checked);

  void pageDetectManualToggled(bool checked);

  void pageDetectDisableToggled(bool checked);

  void autoMarginsToggled(bool checked);

  void alignmentModeChanged(int);

  void matchSizeModeChanged(int);

  void updateTargetSizeEnabled();

  void topBottomLinkClicked();

  void leftRightLinkClicked();

  void horMarginsChanged(double val);

  void vertMarginsChanged(double val);

  void colorModeChanged(output::ColorMode colorMode);

  void thresholdMethodChanged(int idx);

  void pictureShapeChanged(int idx);

  void equalizeIlluminationToggled(bool checked);

  void equalizeIlluminationColorToggled(bool checked);

  void splittingToggled(bool checked);

  void bwForegroundToggled(bool checked);

  void colorForegroundToggled(bool checked);

  void thresholdDeltaChanged(double value);

  void colorSegmentationToggled(bool checked);

  void updateColorReductionControls();

  void colorReductionChanged(int index);

  void dpiSelectionChanged();

  void fillOffcutToggled(bool checked);

  void fillOutsidePageBoxToggled(bool checked);

  void chromaToggled(bool checked);

  void subtractBgToggled(bool checked);

  void knnToggled(bool checked);

  void sharpenToggled(bool checked);

  void wienerToggled(bool checked);

  void dewarpingModeChanged();

  void depthPerceptionChangedSlot(int val);

  void despeckleToggled(bool checked);

  void despeckleSliderValueChanged(int value);

  void profileChanged(int index);

  void profileSavePressed();

  void profileDeletePressed();

  void commitChanges();

 private:
  // The color mode of the pressed mode button.
  output::ColorMode currentColorMode() const;

  void updateFixOrientationDisplay(const DefaultParams::FixOrientationParams& params);

  void updatePageSplitDisplay(const DefaultParams::PageSplitParams& params);

  void updateDeskewDisplay(const DefaultParams::DeskewParams& params);

  void updateSelectContentDisplay(const DefaultParams::SelectContentParams& params);

  void updatePageLayoutDisplay(const DefaultParams::PageLayoutParams& params);

  void updateOutputDisplay(const DefaultParams::OutputParams& params);

  void updateUnits(Units units);

  void setupUiConnections();

  void setRotation(const OrthogonalRotation& rotation);

  void setRotationPixmap();

  void updateAlignmentButtonsEnabled();

  void updateAutoModeButtons();

  void updateAlignmentModeEnabled();

  QToolButton* getCheckedAlignmentButton() const;

  void setLinkButtonLinked(QToolButton* button, bool linked);

  void loadParams(const DefaultParams& params);

  std::unique_ptr<DefaultParams> buildParams() const;

  bool isProfileNameReserved(const QString& name);

  void setTabWidgetsEnabled(bool enabled);

  void setupIcons();

  void fitToContents();

  void updateBinarizationPageSizes();

  void updateEqualizeIlluminationDisplay();

  QIcon m_chainIcon;
  QIcon m_brokenChainIcon;
  bool m_leftRightLinkEnabled;
  bool m_topBottomLinkEnabled;
  OrthogonalRotation m_orthogonalRotation;
  std::unordered_map<QToolButton*, page_layout::Alignment> m_alignmentByButton;
  QButtonGroup* m_alignmentButtonGroup;
  DefaultParamsProfileManager m_profileManager;
  QButtonGroup* m_dewarpingModeGroup;
  // The output resolution shown before the current editing, restored after an invalid value.
  int m_outputDpi = 600;
  // A strength of 0 turns "Subtract background" off; turning it on again restores the last strength.
  double m_subtractBgCoefWhenOn = 1.0;
  // A strength of 0 turns the KNN denoiser off; turning it on again restores the last strength.
  double m_knnCoefWhenOn = 0.3;
  // A strength of 0 turns sharpening off; turning it on again restores the last strength.
  double m_sharpenCoefWhenOn = 1.0;
  // A strength of 0 turns the Wiener denoiser off; turning it on again restores the last strength.
  double m_wienerCoefWhenOn = 0.1;
  bool m_checkingDpi = false;
  bool m_fittedToContents = false;
  // The two stored "Equalize illumination" settings; which check box shows which depends on the
  // color mode.
  bool m_bwNormalizeIllumination = false;
  bool m_colorNormalizeIllumination = false;
  // The binarization method shown, to know which one the selector switches from.
  int m_binarizationMethod = 0;
  // "Reduce colors" of the text (0 none, 1 posterize, 2 K-means) and of whole color pages, kept
  // apart, as the list shows the one of the chosen color mode.
  int m_textColorReduction = 0;
  bool m_pagePosterize = false;
  int m_customProfileItemIdx;
  Units m_currentUnits;
  std::set<QString> m_reservedProfileNames;

  ConnectionManager m_connectionManager;
};


#endif  // SCANTAILOR_APP_DEFAULTPARAMSDIALOG_H_
