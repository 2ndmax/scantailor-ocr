// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_
#define SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_

#include <core/ConnectionManager.h>

#include <QtCore/QObjectCleanupHandler>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QStackedLayout>
#include <list>
#include <memory>
#include <set>

#include "ColorParams.h"
#include "DepthPerception.h"
#include "DespeckleLevel.h"
#include "DewarpingOptions.h"
#include "Dpi.h"
#include "FilterOptionsWidget.h"
#include "ImageViewTab.h"
#include "OptionsWidgetBinarization.h"
#include "OutputProcessingParams.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"
#include "Params.h"
#include "ui_OptionsWidget.h"

namespace dewarping {
class DistortionModel;
}

namespace output {
class Settings;

class OptionsWidget : public FilterOptionsWidget, private Ui::OptionsWidget {
  Q_OBJECT
 public:
  OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor);

  ~OptionsWidget() override;

  void preUpdateUI(const PageId& pageId);

  void postUpdateUI();

  ImageViewTab lastTab() const;

  const DepthPerception& depthPerception() const;

 signals:

  void despeckleLevelChanged(double level, bool* handled);

  void depthPerceptionChanged(double val);

 public slots:

  void tabChanged(ImageViewTab tab);

  void distortionModelChanged(const dewarping::DistortionModel& model);

 private slots:

  void dpiSelectionChanged();

  void applyDpiButtonClicked();

  void applyDpiConfirmed(const std::set<PageId>& pages);

  void applyColorsButtonClicked();

  void applySplittingButtonClicked();

  void dpiChanged(const std::set<PageId>& pages, const Dpi& dpi);

  void applyColorsConfirmed(const std::set<PageId>& pages);

  void applySplittingOptionsConfirmed(const std::set<PageId>& pages);

  void colorModeChanged(int idx);

  void blackOnWhiteToggled(bool value);

  void applyProcessingParamsClicked();

  void applyProcessingParamsConfirmed(const std::set<PageId>& pages);

  void thresholdMethodChanged(int idx);

  void fillingColorChanged(int idx);

  void pictureShapeChanged(int idx);

  void pictureShapeSensitivityChanged(int value);

  void higherSearchSensivityToggled(bool checked);

  void wienerToggled(bool checked);

  void wienerCoefChanged(double value);

  void wienerWindowSizeChanged(int value);

  void colorSegmentationToggled(bool checked);

  void reduceNoiseChanged(int value);

  void redAdjustmentChanged(int value);

  void greenAdjustmentChanged(int value);

  void blueAdjustmentChanged(int value);

  void posterizeToggled(bool checked);

  void posterizeLevelChanged(int value);

  void posterizeNormalizationToggled(bool checked);

  void posterizeForceBwToggled(bool checked);

  void fillMarginsToggled(bool checked);

  void fillOffcutToggled(bool checked);

  void fillOutsidePageBoxToggled(bool checked);

  void equalizeIlluminationToggled(bool checked);

  void equalizeIlluminationColorToggled(bool checked);

  void grayscaleOutputToggled(bool checked);

  void savitzkyGolaySmoothingToggled(bool checked);

  void morphologicalSmoothingToggled(bool checked);

  void splittingToggled(bool checked);

  void bwForegroundToggled(bool checked);

  void colorForegroundToggled(bool checked);

  void originalBackgroundToggled(bool checked);

  void binarizationSettingsChanged();

  void despeckleToggled(bool checked);

  void despeckleSliderReleased();

  void despeckleSliderValueChanged(int value);

  void applyDespeckleButtonClicked();

  void applyDespeckleConfirmed(const std::set<PageId>& pages);

  void dewarpingModeChanged(int mode);

  void dewarpingPostDeskewToggled(bool checked);

  void applyDewarpingButtonClicked();

  void applyDewarpingConfirmed(const std::set<PageId>& pages);

  void dewarpingChanged(const std::set<PageId>& pages, const DewarpingOptions& opt);

  void applyDepthPerceptionButtonClicked();

  void applyDepthPerceptionConfirmed(const std::set<PageId>& pages);

  void depthPerceptionChangedSlot(int val);

  void updateBinarizationOptionsDisplay(int idx);

  void sendReloadRequested();

 private:
  void handleDespeckleLevelChange(double level, bool delay = false);

  void reloadIfNecessary();

  void updateDpiDisplay();

  void updateColorsDisplay();

  void updateDewarpingDisplay();

  void updateProcessingDisplay();

  void addOptionsWidgetBinarization(OptionsWidgetBinarization* widget);

  void setupUiConnections();

  void setupTiffCompressionPanel();

  void tiffCompressionChanged(bool delayReload);

  std::shared_ptr<Settings> m_settings;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  Dpi m_outputDpi;
  ColorParams m_colorParams;
  SplittingOptions m_splittingOptions;
  PictureShapeOptions m_pictureShapeOptions;
  DepthPerception m_depthPerception;
  DewarpingOptions m_dewarpingOptions;
  double m_despeckleLevel;
  // A strength of 0 turns the Wiener denoiser off; turning it on again restores the last strength.
  double m_wienerCoefWhenOn = 0.1;
  ImageViewTab m_lastTab;
  QTimer m_delayedReloadRequest;
  bool m_checkingDpi = false;
  QButtonGroup* m_dewarpingModeGroup = nullptr;

  ConnectionManager m_connectionManager;
  QComboBox* m_tiffColorCompression = nullptr;
  QSpinBox* m_tiffJpegQuality = nullptr;
  QComboBox* m_tiffPaletteCompression = nullptr;
  QComboBox* m_tiffBwCompression = nullptr;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_
