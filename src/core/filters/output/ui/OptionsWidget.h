// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_
#define SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_

#include <ConnectionManager.h>

#include <QTimer>
#include <memory>
#include <set>

#include "../ColorParams.h"
#include "../DepthPerception.h"
#include "../DespeckleLevel.h"
#include "../DewarpingOptions.h"
#include "../ImageViewTab.h"
#include "../LastImageViewTab.h"
#include "../OutputProcessingParams.h"
#include "../Params.h"
#include "Dpi.h"
#include "FilterOptionsWidget.h"
#include "OptionsWidgetBinarization.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"
#include "ui_OptionsWidget.h"

struct ProcessingOptions;
class TestOutputQt;
class TiffCompressionPanel;

namespace dewarping {
class DistortionModel;
}

namespace output {
class ApplyParamsDialog;
class PhotoAdjustments;
class PhotoAdjustmentsControls;
class Settings;

class OptionsWidget : public FilterOptionsWidget, private Ui::OptionsWidget {
  Q_OBJECT
  friend class ::TestOutputQt;

 public:
  /** How long after the last change of a setting the page is processed again, in milliseconds. */
  static constexpr int RELOAD_DELAY_MS = 750;

  OptionsWidget(std::shared_ptr<Settings> settings,
                std::shared_ptr<ProcessingOptions> processingOptions,
                const PageSelectionAccessor& pageSelectionAccessor,
                std::shared_ptr<LastImageViewTab> lastTab);

  ~OptionsWidget() override;

  void preUpdateUI(const PageId& pageId);

  void postUpdateUI();

  ImageViewTab lastTab() const;

  const DepthPerception& depthPerception() const;

  bool hasPendingReload() const { return m_delayedReloadRequest.isActive(); }

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

  void applyColorsConfirmed(const std::set<PageId>& pages, bool applyColorMode, bool applyOtherSettings);

  void applySplittingOptionsConfirmed(const std::set<PageId>& pages);

  void colorModeChanged(ColorMode mode);

  void colorDetectionMinColorChanged(int percent);

  void colorDetectionZonesToggled(bool checked);

  void colorDetectionMinPictureChanged(int percent);

  void colorDetectionButtonClicked();

  void blackOnWhiteToggled(bool value);

  void applyProcessingParamsClicked();

  void applyProcessingParamsConfirmed(const std::set<PageId>& pages);

  void thresholdMethodChanged(int idx);

  void fillingColorChanged(int idx);

  void pictureShapeChanged(int idx);

  void pictureShapeSensitivityChanged(int value);

  void higherSearchSensivityToggled(bool checked);

  void chromaToggled(bool checked);

  void chromaRadiusChanged(int value);

  void chromaGrayLimitChanged(int value);

  void subtractBgWhiteToggled(bool checked);

  void colorSegmentationToggled(bool checked);

  void reduceNoiseChanged(int value);

  void redAdjustmentChanged(int value);

  void greenAdjustmentChanged(int value);

  void blueAdjustmentChanged(int value);

  void colorReductionChanged(int index);

  void kmeansColorsChanged(int value);

  void kmeansFixBlackToggled(bool checked);

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

  void coloredDimmingChanged(double value);

  void binarizationBoundChanged();

  void splittingToggled(bool checked);

  void bwForegroundToggled(bool checked);

  void colorForegroundToggled(bool checked);

  void originalBackgroundToggled(bool checked);

  void binarizationSettingsChanged();

  void binarizationColorParamsChanged();

  void despeckleToggled(bool checked);

  void despeckleSliderReleased();

  void despeckleSliderValueChanged(int value);

  void applyDespeckleButtonClicked();

  void applyDespeckleConfirmed(const std::set<PageId>& pages);

  void dewarpingToggled(bool on);

  void dewarpingMethodActivated();

  void dewarpingModeChanged(DewarpingMode mode);

  void dewarpingPostDeskewToggled(bool checked);

  void evenOutLetterWidthsToggled(bool checked);

  void applyDewarpingButtonClicked();

  void applyDewarpingConfirmed(const std::set<PageId>& pages);

  void dewarpingChanged(const std::set<PageId>& pages, const DewarpingOptions& opt);

  void depthPerceptionChangedSlot(int val);

  void depthPerceptionSliderReleased();

  void updateBinarizationOptionsDisplay(int idx);

  void sendReloadRequested();

  void applyPhotoAdjustmentsButtonClicked();

  void applyPhotoAdjustmentsConfirmed(const std::set<PageId>& pages);

 private:
  /// The chosen "Reduce colors" of the color segmentation: 0 none, 1 posterize, 2 K-means.
  /// Whether the color mode reduces the colors of the text (black and white, mixed) rather than
  /// those of the whole page (color).
  bool reducesTextColors() const;

  int colorReduction() const;

  void updateColorReductionControls();

  void commitPhotoAdjustments(const PhotoAdjustments& adjustments);

  ApplyParamsDialog* createApplyDialog(const QString& title = QString());

  template <typename OnAccepted>
  ApplyParamsDialog* openApplyDialog(const QString& title, OnAccepted&& onAccepted);

  void updateEnhancementFilter(QCheckBox* cb,
                               QWidget* optionsWidget,
                               QDoubleSpinBox* coefBox,
                               QSpinBox* paramBox,
                               double coef,
                               double& coefWhenOn,
                               int param);

  template <typename GetCoef, typename SetCoef, typename SetParam>
  void connectEnhancementFilter(QCheckBox* cb,
                                QWidget* optionsWidget,
                                QDoubleSpinBox* coefBox,
                                QSpinBox* paramBox,
                                double& coefWhenOn,
                                GetCoef getCoef,
                                SetCoef setCoef,
                                SetParam setParam);

  void selectBinarizationPanel(BinarizationMethod method);

  void requestReloadNow();

  void handleDespeckleLevelChange(double level, bool delay = false);

  void reloadIfNecessary();

  void invalidateThumbnails(const std::set<PageId>& pages);

  // After settings were applied to the pages: their thumbnails, and the current page if among them.
  void pagesChanged(const std::set<PageId>& pages);

  void updateDpiDisplay();

  void updateColorsDisplay();

  void updateDewarpingDisplay();

  void updateProcessingDisplay();

  void addOptionsWidgetBinarization(OptionsWidgetBinarization* widget);

  /** Shows the stored values in the option widgets of all binarization methods. */
  void updateBinarizationWidgets();

  /** The original background is only kept behind a black and white foreground. */
  void updateOriginalBackgroundEnabled();

  void setupUiConnections();

  void setupTiffCompressionPanel();

  void tiffCompressionChanged(bool delayReload);

  std::shared_ptr<Settings> m_settings;
  // The TIFF compression and the color mode detection of the project.
  std::shared_ptr<ProcessingOptions> m_processingOptions;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  Dpi m_outputDpi;
  ColorParams m_colorParams;
  SplittingOptions m_splittingOptions;
  PictureShapeOptions m_pictureShapeOptions;
  DepthPerception m_depthPerception;
  DewarpingOptions m_dewarpingOptions;
  double m_despeckleLevel;
  // A strength of 0 turns "Subtract background" off; turning it on again restores the last strength.
  double m_subtractBgCoefWhenOn = 1.0;
  // A strength of 0 turns the KNN denoiser off; turning it on again restores the last strength.
  double m_knnCoefWhenOn = 0.3;
  // A strength of 0 turns sharpening off; turning it on again restores the last strength.
  double m_sharpenCoefWhenOn = 1.0;
  // A strength of 0 turns the Wiener denoiser off; turning it on again restores the last strength.
  double m_wienerCoefWhenOn = 0.1;
  // Shared with the Filter, whose tasks prepare what the tab shows.
  std::shared_ptr<LastImageViewTab> m_lastTab;
  // A member, so it stops with the widget: no delayed reload fires after the widget is gone.
  QTimer m_delayedReloadRequest;
  bool m_checkingDpi = false;
  PhotoAdjustmentsControls* m_photoAdjustmentsControls = nullptr;

  ConnectionManager m_connectionManager;
  TiffCompressionPanel* m_tiffCompressionPanel = nullptr;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_OPTIONSWIDGET_H_
