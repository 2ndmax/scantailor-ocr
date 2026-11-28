// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include "DewarpingMethodBox.h"

#include <tiff.h>

#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QToolTip>
#include <algorithm>
#include <utility>

#include "../FillZoneComparator.h"
#include "../PictureZoneComparator.h"
#include "ApplyParamsDialog.h"
#include "BinarizationMethods.h"
#include "CoreUtils.h"
#include "OptionsWidgetBinarizationLocal.h"
#include "OptionsWidgetBinarizationOtsu.h"
#include "PhotoAdjustmentsControls.h"
#include "ProcessingOptions.h"
#include "TiffCompressionPanel.h"
#include "WidgetUtils.h"

using namespace core;

namespace output {
namespace {
// Shows the value of a slider in its label and, right away, as a tooltip at the handle.
void showSliderValue(QSlider* slider, QLabel* valueLabel, const double value) {
  const QString text(QLocale().toString(value, 'f', 1));
  slider->setToolTip(text);
  valueLabel->setText(text);

  QPoint tooltipPos(slider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(slider->rect().center().y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), slider->width()));
  QToolTip::showText(slider->mapToGlobal(tooltipPos), text, slider);
}
}  // namespace

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings,
                             std::shared_ptr<ProcessingOptions> processingOptions,
                             const PageSelectionAccessor& pageSelectionAccessor,
                             std::shared_ptr<LastImageViewTab> lastTab)
    : m_settings(std::move(settings)),
      m_processingOptions(std::move(processingOptions)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_despeckleLevel(1.0),
      m_lastTab(std::move(lastTab)),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  // Percent fields are as wide as "100 %", their highest value.
  for (QSpinBox* spinBox : {colorDetectionMinColorSB, colorDetectionMinPictureSB, pictureShapeSensitivitySB}) {
    core::WidgetUtils::setNumberFieldWidth(spinBox, 100);
  }
  // Pixel fields of the filters are as wide as "999 px", fields without a unit as "-100".
  for (QSpinBox* spinBox : {chromaRadius, subtractBgRadius, knnRadius, wienerWindowSize, sharpenRadius}) {
    core::WidgetUtils::setNumberFieldWidth(spinBox, 999);
  }
  for (QAbstractSpinBox* spinBox : std::initializer_list<QAbstractSpinBox*>{
           chromaGrayLimit, subtractBgCoef, knnCoef, wienerCoef, sharpenCoef, redAdjustmentSB, greenAdjustmentSB,
           blueAdjustmentSB, reduceNoiseSB, kmeansColorsSB, posterizeLevelSB}) {
    core::WidgetUtils::setNumberFieldWidth(spinBox, -100, 0);
  }
  // As wide as the other fields without a unit.
  for (QAbstractSpinBox* spinBox :
       std::initializer_list<QAbstractSpinBox*>{coloredDimmingSB, lowerBoundSB, upperBoundSB}) {
    core::WidgetUtils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_delayedReloadRequest.setSingleShot(true);

  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(0.5 * 10));
  despeckleSlider->setMaximum(qRound(3.5 * 10));


  fillBinarizationMethodBox(thresholdMethodBox);

  fillingColorBox->addItem(tr("Background"), FILL_BACKGROUND);
  fillingColorBox->addItem(tr("White"), FILL_WHITE);
  fillingColorBox->addItem(tr("Black"), FILL_BLACK);

  while (binarizationOptions->count() != 0) {
    QWidget* dummy = binarizationOptions->widget(0);
    binarizationOptions->removeWidget(dummy);
    delete dummy;
  }
  addOptionsWidgetBinarization(new OptionsWidgetBinarizationOtsu(m_settings));
  // One panel per method, in the order of BinarizationMethod.
  for (int i = T_SAUVOLA; i <= T_MULTISCALE; ++i) {
    addOptionsWidgetBinarization(new OptionsWidgetBinarizationLocal(m_settings, static_cast<BinarizationMethod>(i)));
  }
  QList<QGridLayout*> binarizationGrids = binarizationOptions->findChildren<QGridLayout*>();
  binarizationGrids.append(allMethodsLayout);
  core::WidgetUtils::alignLabelColumns(binarizationGrids);
  // The fields of the enhancement filters start at the same place; column 0 is the indent.
  core::WidgetUtils::alignLabelColumns(
      {chromaOptionsLayout, subtractBgOptionsLayout, knnOptionsLayout, wienerOptionsLayout, sharpenOptionsLayout}, 1);
  // The settings of the threshold method are indented below the method list, as the settings of
  // the enhancement filters below their check boxes (a 15 px indent column plus the spacing).
  const int methodIndent = 15 + chromaOptionsLayout->horizontalSpacing();
  binarizationOptions->setContentsMargins(methodIndent, 0, 0, 0);
  // "Reduce colors" starts where "Reduce noise" starts, its settings one indent further. The list
  // has its own row, so that its width doesn't move the fields of the settings.
  colorReductionChoiceLayout->setSpacing(segmenterReductionLayout->horizontalSpacing());
  colorReductionLayout->setHorizontalSpacing(segmenterReductionLayout->horizontalSpacing());
  updateBinarizationOptionsDisplay(binarizationOptions->currentIndex());

  // The order follows OptionsWidget::colorReduction().
  colorReductionCB->addItem(tr("None", "color reduction"));
  colorReductionCB->addItem(tr("Posterize"));
  colorReductionCB->addItem(tr("K-means"));

  pictureShapeSelector->addItem(tr("Off"), OFF_SHAPE);
  pictureShapeSelector->addItem(tr("Contour mask"), FREE_SHAPE);
  pictureShapeSelector->addItem(tr("Rectangle"), RECTANGULAR_SHAPE);

  // Common resolutions; others can be typed in.
  for (const int dpi : {300, 400, 600, 1200}) {
    dpiSelector->addItem(CoreUtils::dpiText(dpi));
  }
  dpiSelector->setValidator(WidgetUtils::createDpiValidator(dpiSelector));
  WidgetUtils::setDpiFieldWidth(dpiSelector);

  DewarpingMethodBox::setUp(dewarpingMethodBox, dewarpingMethodLabel);

  m_photoAdjustmentsControls = new PhotoAdjustmentsControls(photoAdjustmentsPanel, colorBalanceGrid, toneGrid,
                                                            colorBalanceHeading, keepWhiteCB, this);

  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();

  connect(binarizationOptions, &QStackedWidget::currentChanged, this, &OptionsWidget::updateBinarizationOptionsDisplay);

  setupTiffCompressionPanel();
  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::setupTiffCompressionPanel() {
  // Built like the compression panel of the Export step. Unlike the other panels of this
  // step, it applies to all pages, so there is no "Apply to ..." button.
  auto* group = new CollapsibleGroupBox(tr("TIFF Compression"));
  group->setObjectName("tiffCompressionPanel");
  group->setToolTip(
      tr("Compression of the TIFF files in the output folder. Applies to the whole project. The PDF is compressed "
         "separately, so this doesn't change it. After a change, the output is created again: the current page "
         "right away, the other pages during the next batch processing."));
  auto* layout = new QVBoxLayout(group);
  m_tiffCompressionPanel = new TiffCompressionPanel;
  m_tiffCompressionPanel->setCompression(m_processingOptions->tiffCompression);
  layout->addWidget(m_tiffCompressionPanel);

  // At the bottom, above the spacer that pushes the panels up.
  verticalLayout_3->insertWidget(verticalLayout_3->count() - 1, group);

  connect(m_tiffCompressionPanel, &TiffCompressionPanel::compressionChanged, this,
          &OptionsWidget::tiffCompressionChanged);
}

void OptionsWidget::tiffCompressionChanged(const bool delayReload) {
  const TiffWriter::Compression old = m_processingOptions->tiffCompression;
  const TiffWriter::Compression compression = m_tiffCompressionPanel->compression();
  m_processingOptions->tiffCompression = compression;

  const bool changed = (compression.color != old.color) || (compression.palette != old.palette)
                       || (compression.bw != old.bw)
                       || ((compression.color == COMPRESSION_JPEG) && (compression.jpegQuality != old.jpegQuality));
  if (!changed) {
    return;
  }

  // The existing output files keep their compression unless they are written again.
  m_settings->removeAllOutputParams();
  emit invalidateAllThumbnails();
  if (delayReload) {
    // Don't recreate the page for every step of the spin box.
    m_delayedReloadRequest.start(RELOAD_DELAY_MS);
  } else {
    requestReloadNow();
  }
}

void OptionsWidget::preUpdateUI(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  if (pageId != m_pageId) {
    // A pending reload of the previous page is dropped, and so is its processing if it hasn't
    // finished, which would have updated its thumbnail. The thumbnail shows then whether the
    // page needs processing.
    m_delayedReloadRequest.stop();
    if (!m_pageId.isNull()) {
      emit invalidateThumbnail(m_pageId);
    }
  }

  const Params params = m_settings->getParams(pageId);
  m_pageId = pageId;
  // The project may have been loaded after this panel was built.
  m_tiffCompressionPanel->setCompression(m_processingOptions->tiffCompression);
  m_outputDpi = params.outputDpi();
  m_colorParams = params.colorParams();
  m_splittingOptions = params.splittingOptions();
  m_pictureShapeOptions = params.pictureShapeOptions();
  m_dewarpingOptions = params.dewarpingOptions();
  m_depthPerception = params.depthPerception();
  m_despeckleLevel = params.despeckleLevel();

  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();
  updateProcessingDisplay();
}

void OptionsWidget::postUpdateUI() {
  auto block = m_connectionManager.getScopedBlock();

  updateProcessingDisplay();
  m_dewarpingOptions = m_settings->getParams(m_pageId).dewarpingOptions();
  updateDewarpingDisplay();
}

void OptionsWidget::tabChanged(const ImageViewTab tab) {
  m_lastTab->set(tab);
  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();
  reloadIfNecessary();
}

void OptionsWidget::distortionModelChanged(const dewarping::DistortionModel& model) {
  m_settings->setDistortionModel(m_pageId, model);

  m_dewarpingOptions.setDewarpingMode(MANUAL);
  m_settings->setDewarpingOptions(m_pageId, m_dewarpingOptions);
  updateDewarpingDisplay();
}

void OptionsWidget::colorModeChanged(const ColorMode mode) {
  m_colorParams.setColorMode(mode);
  m_settings->setColorParams(m_pageId, m_colorParams);
  updateColorsDisplay();
  requestReloadNow();
}

void OptionsWidget::colorDetectionMinColorChanged(const int percent) {
  m_processingOptions->colorDetection.minColorShare = percent / 100.0;
}

void OptionsWidget::colorDetectionZonesToggled(const bool checked) {
  m_processingOptions->colorDetection.usePictureZones = checked;
  colorDetectionMinPictureLabel->setEnabled(checked);
  colorDetectionMinPictureSB->setEnabled(checked);
}

void OptionsWidget::colorDetectionMinPictureChanged(const int percent) {
  m_processingOptions->colorDetection.minPictureShare = percent / 100.0;
}

void OptionsWidget::colorDetectionButtonClicked() {
  ApplyParamsDialog* dialog = openApplyDialog(tr("Detect Color Mode"), &OptionsWidget::colorModeDetectionRequested);
  dialog->setThisPageIncluded(true);
}

void OptionsWidget::thresholdMethodChanged(int idx) {
  const auto method = static_cast<BinarizationMethod>(thresholdMethodBox->itemData(idx).toInt());
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  blackWhiteOptions.setBinarizationMethod(method);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  selectBinarizationPanel(method);

  requestReloadNow();
}

void OptionsWidget::fillingColorChanged(int idx) {
  const FillingColor color = (FillingColor) fillingColorBox->itemData(idx).toInt();
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillingColor(color);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  requestReloadNow();
}

void OptionsWidget::pictureShapeChanged(const int idx) {
  const auto shapeMode = static_cast<PictureShape>(pictureShapeSelector->itemData(idx).toInt());
  m_pictureShapeOptions.setPictureShape(shapeMode);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  pictureShapeSensitivityOptions->setEnabled(shapeMode == RECTANGULAR_SHAPE);
  higherSearchSensitivityCB->setEnabled(shapeMode != OFF_SHAPE);

  requestReloadNow();
}

void OptionsWidget::pictureShapeSensitivityChanged(int value) {
  m_pictureShapeOptions.setSensitivity(value);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::higherSearchSensivityToggled(const bool checked) {
  m_pictureShapeOptions.setHigherSearchSensitivity(checked);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  requestReloadNow();
}

void OptionsWidget::fillMarginsToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillMargins(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::fillOffcutToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillOffcut(checked);
  if (checked) {
    colorCommonOptions.setFillOutsidePageBox(false);
    // Only clicks are connected, so this doesn't call fillOutsidePageBoxToggled().
    fillOutsidePageBoxCB->setChecked(false);
  }
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::fillOutsidePageBoxToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillOutsidePageBox(checked);
  if (checked) {
    colorCommonOptions.setFillOffcut(false);
    // Only clicks are connected, so this doesn't call fillOffcutToggled().
    fillOffcutCB->setChecked(false);
  }
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::equalizeIlluminationToggled(const bool checked) {
  // One check box for the setting the color mode uses: the color one in color mode, otherwise
  // the black and white one.
  if (m_colorParams.colorMode() == COLOR_GRAYSCALE) {
    ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
    colorCommonOptions.setNormalizeIllumination(checked);
    m_colorParams.setColorCommonOptions(colorCommonOptions);
    equalizeIlluminationColorCB->setChecked(checked);
  } else {
    BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
    blackWhiteOptions.setNormalizeIllumination(checked);
    m_colorParams.setBlackWhiteOptions(blackWhiteOptions);

    if (m_colorParams.colorMode() == MIXED) {
      // The color setting is kept; it only applies together with this one (see RenderParams).
      equalizeIlluminationColorCB->setChecked(checked && m_colorParams.colorCommonOptions().normalizeIllumination());
      equalizeIlluminationColorCB->setEnabled(checked);
    }
  }

  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::equalizeIlluminationColorToggled(const bool checked) {
  ColorCommonOptions opt(m_colorParams.colorCommonOptions());
  opt.setNormalizeIllumination(checked);
  m_colorParams.setColorCommonOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::grayscaleOutputToggled(const bool checked) {
  ColorCommonOptions opt(m_colorParams.colorCommonOptions());
  opt.setGrayscaleOutput(checked);
  m_colorParams.setColorCommonOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
  emit invalidateThumbnail(m_pageId);
}


void OptionsWidget::binarizationSettingsChanged() {
  requestReloadNow();
  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::binarizationColorParamsChanged() {
  // The binarization widgets write the settings themselves. Keep our copy current,
  // or the next change made here would write their old values back.
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
}

void OptionsWidget::dpiSelectionChanged() {
  // The message box takes the focus, which finishes the editing a second time.
  if (m_checkingDpi) {
    return;
  }
  bool ok = false;
  const int dpi = CoreUtils::dpiFromText(dpiSelector->currentText(), &ok);
  if (!ok || (dpi < 72) || (dpi > 1200)) {
    m_checkingDpi = true;
    QMessageBox::warning(this, tr("Output Resolution"), tr("The resolution must be between 72 and 1200 DPI."));
    updateDpiDisplay();
    m_checkingDpi = false;
    return;
  }
  if (Dpi(dpi, dpi) == m_outputDpi) {
    return;
  }
  dpiChanged({m_pageId}, Dpi(dpi, dpi));
}

void OptionsWidget::applyDpiButtonClicked() {
  openApplyDialog(tr("Apply Output Resolution"), &OptionsWidget::applyDpiConfirmed);
}

void OptionsWidget::applyDpiConfirmed(const std::set<PageId>& pages) {
  dpiChanged(pages, m_outputDpi);
}

void OptionsWidget::applyColorsButtonClicked() {
  ApplyParamsDialog* dialog = createApplyDialog();
  dialog->showModeParts();
  connect(dialog, &ApplyParamsDialog::accepted, this, [this, dialog](const std::set<PageId>& pages) {
    applyColorsConfirmed(pages, dialog->isColorModeChosen(), dialog->isOtherSettingsChosen());
  });
}

void OptionsWidget::dpiChanged(const std::set<PageId>& pages, const Dpi& dpi) {
  for (const PageId& pageId : pages) {
    Params params(m_settings->getParams(pageId));
    // Keeps the curves found automatically, so they are not searched again at the new resolution.
    // Without curves found yet, "Manual" would mean no dewarping at all, so the page stays on its
    // method. A method that found nothing stores a flat stand-in, which is valid but no curves found.
    const DewarpingMode mode = params.dewarpingOptions().dewarpingMode();
    const bool curvesFound = params.distortionModel().isValid() && !params.dewarpingOptions().noModelFound();
    if (((mode == AUTO) || (mode == TRAPEZOID)) && curvesFound) {
      DewarpingOptions opt(params.dewarpingOptions());
      opt.setDewarpingMode(MANUAL);
      m_settings->setDewarpingOptions(pageId, opt);
    }
    m_settings->setDpi(pageId, dpi);
  }

  invalidateThumbnails(pages);

  if (pages.find(m_pageId) != pages.end()) {
    m_outputDpi = dpi;
    updateDpiDisplay();
    // The dewarping mode may have changed above.
    m_dewarpingOptions = m_settings->getParams(m_pageId).dewarpingOptions();
    updateDewarpingDisplay();
    requestReloadNow();
  }
}

void OptionsWidget::applyColorsConfirmed(const std::set<PageId>& pages,
                                         const bool applyColorMode,
                                         const bool applyOtherSettings) {
  for (const PageId& pageId : pages) {
    const ColorParams pageColorParams = m_settings->getParams(pageId).colorParams();
    // The color mode and the other settings of the Mode group can be applied separately.
    ColorParams colorParams = applyOtherSettings ? m_colorParams : pageColorParams;
    // The photo adjustments have their own "Apply to ...".
    colorParams.setPhotoAdjustments(pageColorParams.photoAdjustments());
    colorParams.setColorMode(applyColorMode ? m_colorParams.colorMode() : pageColorParams.colorMode());
    m_settings->setColorParams(pageId, colorParams);
    if (applyOtherSettings) {
      m_settings->setPictureShapeOptions(pageId, m_pictureShapeOptions);
    }
  }

  pagesChanged(pages);
}

void OptionsWidget::commitPhotoAdjustments(const PhotoAdjustments& adjustments) {
  if (adjustments == m_colorParams.photoAdjustments()) {
    return;
  }

  m_colorParams.setPhotoAdjustments(adjustments);
  m_settings->setColorParams(m_pageId, m_colorParams);
  // Don't recreate the page for every step of a field or of the keyboard.
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::applyPhotoAdjustmentsButtonClicked() {
  openApplyDialog(tr("Apply Photo Adjustments"), &OptionsWidget::applyPhotoAdjustmentsConfirmed);
}

void OptionsWidget::applyPhotoAdjustmentsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    ColorParams colorParams = m_settings->getParams(pageId).colorParams();
    colorParams.setPhotoAdjustments(m_colorParams.photoAdjustments());
    m_settings->setColorParams(pageId, colorParams);
  }

  pagesChanged(pages);
}

void OptionsWidget::applySplittingButtonClicked() {
  openApplyDialog(tr("Apply Splitting"), &OptionsWidget::applySplittingOptionsConfirmed);
}

void OptionsWidget::applySplittingOptionsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setSplittingOptions(pageId, m_splittingOptions);
  }

  pagesChanged(pages);
}

void OptionsWidget::despeckleToggled(bool checked) {
  if (checked) {
    handleDespeckleLevelChange(0.1 * despeckleSlider->value());
  } else {
    handleDespeckleLevelChange(0);
  }

  despeckleSlider->setEnabled(checked);
  despeckleValueLabel->setEnabled(checked);
}

void OptionsWidget::despeckleSliderReleased() {
  const double value = 0.1 * despeckleSlider->value();
  handleDespeckleLevelChange(value);
}

void OptionsWidget::despeckleSliderValueChanged(int value) {
  const double newValue = 0.1 * value;

  showSliderValue(despeckleSlider, despeckleValueLabel, newValue);

  if (despeckleSlider->isSliderDown()) {
    return;
  }

  handleDespeckleLevelChange(newValue, true);
}

void OptionsWidget::handleDespeckleLevelChange(const double level, const bool delay) {
  m_despeckleLevel = level;
  m_settings->setDespeckleLevel(m_pageId, level);

  bool handled = false;
  emit despeckleLevelChanged(level, &handled);

  if (handled) {
    // This means we are on the "Despeckling" tab.
    emit invalidateThumbnail(m_pageId);
  } else {
    if (delay) {
      m_delayedReloadRequest.start(RELOAD_DELAY_MS);
    } else {
      requestReloadNow();
    }
  }
}

void OptionsWidget::applyDespeckleButtonClicked() {
  openApplyDialog(tr("Apply Despeckling"), &OptionsWidget::applyDespeckleConfirmed);
}

void OptionsWidget::applyDespeckleConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setDespeckleLevel(pageId, m_despeckleLevel);
  }

  pagesChanged(pages);
}

void OptionsWidget::dewarpingToggled(const bool on) {
  dewarpingModeChanged(on ? DewarpingMethodBox::selectedMethod(dewarpingMethodBox) : OFF);
}

void OptionsWidget::dewarpingMethodActivated() {
  dewarpingModeChanged(DewarpingMethodBox::selectedMethod(dewarpingMethodBox));
}

void OptionsWidget::dewarpingModeChanged(const DewarpingMode mode) {
  DewarpingOptions opt(m_dewarpingOptions);
  opt.setDewarpingMode(mode);
  dewarpingChanged({m_pageId}, opt);
}

void OptionsWidget::dewarpingPostDeskewToggled(const bool checked) {
  DewarpingOptions opt(m_dewarpingOptions);
  opt.setPostDeskew(checked);
  dewarpingChanged({m_pageId}, opt);
}

void OptionsWidget::evenOutLetterWidthsToggled(const bool checked) {
  DewarpingOptions opt(m_dewarpingOptions);
  opt.setEvenOutLetterWidths(checked);
  dewarpingChanged({m_pageId}, opt);
}

void OptionsWidget::applyDewarpingButtonClicked() {
  openApplyDialog(tr("Apply Dewarping"), &OptionsWidget::applyDewarpingConfirmed);
}

void OptionsWidget::applyDewarpingConfirmed(const std::set<PageId>& pages) {
  // The depth perception belongs to the dewarping and is applied with it.
  for (const PageId& pageId : pages) {
    m_settings->setDepthPerception(pageId, m_depthPerception);
  }
  dewarpingChanged(pages, m_dewarpingOptions);
}

void OptionsWidget::dewarpingChanged(const std::set<PageId>& pages, const DewarpingOptions& opt) {
  for (const PageId& pageId : pages) {
    // The angle of the post deskew was found for each page. It is left out when comparing the
    // options, so a page that isn't processed again would keep the angle of another one.
    DewarpingOptions pageOpt(opt);
    const DewarpingOptions oldPageOpt = m_settings->getParams(pageId).dewarpingOptions();
    pageOpt.setPostDeskewAngle(oldPageOpt.getPostDeskewAngle());
    pageOpt.setNoModelFound(oldPageOpt.noModelFound());
    m_settings->setDewarpingOptions(pageId, pageOpt);
  }

  invalidateThumbnails(pages);

  if (pages.find(m_pageId) != pages.end()) {
    if (m_dewarpingOptions != opt) {
      m_dewarpingOptions = opt;

      // We also have to reload if we are currently on the "Fill Zones" tab,
      // as it makes use of original <-> dewarped coordinate mapping,
      // which is too hard to update without reloading. For consistency,
      // we reload not just on TAB_FILL_ZONES but on all tabs except TAB_DEWARPING.
      // PS: the static original <-> dewarped mappings are constructed
      // in Task::UiUpdater::updateUI(). Look for "new DewarpingPointMapper" there.
      // The automatic modes have to find the curves anew, so they reload on the Dewarping tab, too.
      if (opt.findsCurvesItself() || (m_lastTab->get() != TAB_DEWARPING)) {
        // Switch to the Output tab after reloading.
        m_lastTab->set(TAB_OUTPUT);
        // These depend on the value of m_lastTab.
        updateDpiDisplay();
        updateColorsDisplay();
        updateDewarpingDisplay();

        requestReloadNow();
      } else {
        // The Dewarping tab follows the change by itself, only the options panel is updated.
        updateDewarpingDisplay();
      }
    }
  }
}  // OptionsWidget::dewarpingChanged

void OptionsWidget::depthPerceptionChangedSlot(int val) {
  m_depthPerception.setValue(0.1 * val);
  showSliderValue(depthPerceptionSlider, depthPerceptionValueLabel, m_depthPerception.value());

  m_settings->setDepthPerception(m_pageId, m_depthPerception);
  // Propagate the signal.
  emit depthPerceptionChanged(m_depthPerception.value());

  // While dragging, the page is recalculated only once the slider is released.
  if (!depthPerceptionSlider->isSliderDown()) {
    depthPerceptionSliderReleased();
  }
}

void OptionsWidget::depthPerceptionSliderReleased() {
  // The Dewarping tab follows the slider by itself; other tabs need the page recalculated.
  if (m_lastTab->get() != TAB_DEWARPING) {
    m_delayedReloadRequest.start(RELOAD_DELAY_MS);
  }
}

void OptionsWidget::invalidateThumbnails(const std::set<PageId>& pages) {
  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }
}

void OptionsWidget::pagesChanged(const std::set<PageId>& pages) {
  invalidateThumbnails(pages);
  if (pages.find(m_pageId) != pages.end()) {
    requestReloadNow();
  }
}

void OptionsWidget::reloadIfNecessary() {
  ZoneSet savedPictureZones;
  ZoneSet savedFillZones;
  DewarpingOptions savedDewarpingOptions;
  dewarping::DistortionModel savedDistortionModel;
  DepthPerception savedDepthPerception;
  double savedDespeckleLevel = 1.0;

  std::unique_ptr<OutputParams> outputParams(m_settings->getOutputParams(m_pageId));
  if (outputParams) {
    savedPictureZones = outputParams->pictureZones();
    savedFillZones = outputParams->fillZones();
    savedDewarpingOptions = outputParams->outputImageParams().dewarpingOptions();
    savedDistortionModel = outputParams->outputImageParams().distortionModel();
    savedDepthPerception = outputParams->outputImageParams().depthPerception();
    savedDespeckleLevel = outputParams->outputImageParams().despeckleLevel();
  }

  const Params params(m_settings->getParams(m_pageId));

  if (!PictureZoneComparator::equal(savedPictureZones, m_settings->pictureZonesForPage(m_pageId))
      || !FillZoneComparator::equal(savedFillZones, m_settings->fillZonesForPage(m_pageId))
      || (savedDespeckleLevel != params.despeckleLevel())) {
    requestReloadNow();
    return;
  }

  const DewarpingOptions& dewarpingOptions = params.dewarpingOptions();
  const DewarpingMode savedMode = savedDewarpingOptions.dewarpingMode();
  const DewarpingMode mode = dewarpingOptions.dewarpingMode();
  if ((savedMode == OFF) && (mode == OFF)) {
    // Without dewarping, its options don't matter.
    return;
  }

  // Only Manual and Marginal modes apply post-deskew in OutputGenerator.
  const auto postDeskewActive = [](const DewarpingOptions& opt) {
    return opt.needPostDeskew() && ((opt.dewarpingMode() == MANUAL) || (opt.dewarpingMode() == MARGINAL));
  };
  bool reload = (savedDepthPerception.value() != params.depthPerception().value())
                || (savedDewarpingOptions.evenOutLetterWidths() != dewarpingOptions.evenOutLetterWidths())
                || (postDeskewActive(savedDewarpingOptions) != postDeskewActive(dewarpingOptions));
  // The automatic modes find the curves anew, so a changed model doesn't count there.
  const bool sameAutomaticMode = (savedMode == mode) && dewarpingOptions.findsCurvesItself();
  if (!reload && !sameAutomaticMode) {
    reload = !savedDistortionModel.matches(params.distortionModel()) || ((savedMode == OFF) != (mode == OFF));
  }
  if (reload) {
    requestReloadNow();
  }
}  // OptionsWidget::reloadIfNecessary

void OptionsWidget::updateDpiDisplay() {
  // Changing the resolution sets the same value for both directions.
  const QSignalBlocker blocker(dpiSelector);
  dpiSelector->setEditText(CoreUtils::dpiText(std::max(m_outputDpi.horizontal(), m_outputDpi.vertical())));
}

void OptionsWidget::updateColorsDisplay() {
  auto block = m_connectionManager.getScopedBlock();
  const QSignalBlocker minColorBlocker(colorDetectionMinColorSB);
  const QSignalBlocker zonesBlocker(colorDetectionZonesCB);
  const QSignalBlocker minPictureBlocker(colorDetectionMinPictureSB);
  const QSignalBlocker coloredDimmingBlocker(coloredDimmingSB);
  const QSignalBlocker lowerBoundBlocker(lowerBoundSB);
  const QSignalBlocker upperBoundBlocker(upperBoundSB);

  const ColorMode colorMode = m_colorParams.colorMode();
  colorModeBWBtn->setChecked(colorMode == BLACK_AND_WHITE);
  colorModeColorBtn->setChecked(colorMode == COLOR_GRAYSCALE);
  colorModeMixedBtn->setChecked(colorMode == MIXED);
  const ColorModeDetector::Options& colorDetection = m_processingOptions->colorDetection;
  colorDetectionMinColorSB->setValue(qRound(colorDetection.minColorShare * 100));
  colorDetectionZonesCB->setChecked(colorDetection.usePictureZones);
  colorDetectionMinPictureSB->setValue(qRound(colorDetection.minPictureShare * 100));
  // Only used with the picture zones.
  colorDetectionMinPictureLabel->setEnabled(colorDetectionZonesCB->isChecked());
  colorDetectionMinPictureSB->setEnabled(colorDetectionZonesCB->isChecked());

  bool thresholdOptionsVisible = false;
  bool pictureShapeVisible = false;
  bool splittingOptionsVisible = false;
  switch (colorMode) {
    case MIXED:
      pictureShapeVisible = true;
      splittingOptionsVisible = true;
      // fall through
    case BLACK_AND_WHITE:
      thresholdOptionsVisible = true;
      // fall through
    case COLOR_GRAYSCALE:
      break;
  }

  // All settings stay visible; those that the color mode doesn't use are grayed out.
  const ColorCommonOptions& colorCommonOptions = m_colorParams.colorCommonOptions();
  const BlackWhiteOptions& blackWhiteOptions = m_colorParams.blackWhiteOptions();

  fillMarginsCB->setChecked(colorCommonOptions.fillMargins());
  fillOffcutCB->setChecked(colorCommonOptions.fillOffcut());
  fillOutsidePageBoxCB->setChecked(colorCommonOptions.fillOutsidePageBox());
  // "Equalize illumination" shows the setting the color mode uses; "Also in picture zones" is the
  // color setting, used in mixed mode on top of the black and white one.
  equalizeIlluminationCB->setChecked((colorMode == COLOR_GRAYSCALE) ? colorCommonOptions.normalizeIllumination()
                                                                    : blackWhiteOptions.normalizeIllumination());
  // In mixed mode it only applies together with the black and white one (see RenderParams).
  const bool colorEqualizationUsable = (colorMode != MIXED) || blackWhiteOptions.normalizeIllumination();
  equalizeIlluminationColorCB->setChecked(colorCommonOptions.normalizeIllumination() && colorEqualizationUsable);
  equalizeIlluminationColorCB->setEnabled(colorMode == MIXED && blackWhiteOptions.normalizeIllumination());
  grayscaleOutputCB->setChecked(colorCommonOptions.isGrayscaleOutput());
  grayscaleOutputCB->setEnabled(colorMode != BLACK_AND_WHITE);
  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  savitzkyGolaySmoothingCB->setEnabled(thresholdOptionsVisible);
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());
  morphologicalSmoothingCB->setEnabled(thresholdOptionsVisible);
  coloredDimmingSB->setValue(blackWhiteOptions.getColoredDimming());
  lowerBoundSB->setValue(blackWhiteOptions.getLowerBound());
  upperBoundSB->setValue(blackWhiteOptions.getUpperBound());

  pictureShapeOptions->setEnabled(pictureShapeVisible);
  thresholdOptions->setEnabled(thresholdOptionsVisible);
  despecklePanel->setEnabled(thresholdOptionsVisible);

  m_photoAdjustmentsControls->updateDisplay(colorMode, colorCommonOptions.isGrayscaleOutput(),
                                            m_colorParams.photoAdjustments());

  splittingOptions->setEnabled(splittingOptionsVisible);
  splittingCB->setChecked(m_splittingOptions.isSplitOutput());
  switch (m_splittingOptions.getSplittingMode()) {
    case BLACK_AND_WHITE_FOREGROUND:
      bwForegroundRB->setChecked(true);
      break;
    case COLOR_FOREGROUND:
      colorForegroundRB->setChecked(true);
      break;
  }
  originalBackgroundCB->setChecked(m_splittingOptions.isOriginalBackgroundEnabled());
  colorForegroundRB->setEnabled(m_splittingOptions.isSplitOutput());
  bwForegroundRB->setEnabled(m_splittingOptions.isSplitOutput());
  updateOriginalBackgroundEnabled();

  thresholdMethodBox->setCurrentIndex(thresholdMethodBox->findData(blackWhiteOptions.getBinarizationMethod()));
  selectBinarizationPanel(blackWhiteOptions.getBinarizationMethod());

  // The areas to fill are used in every color mode, the color not in black and white.
  fillingColorLabel->setEnabled(colorMode != BLACK_AND_WHITE);
  fillingColorBox->setEnabled(colorMode != BLACK_AND_WHITE);
  fillingColorBox->setCurrentIndex(fillingColorBox->findData(colorCommonOptions.getFillingColor()));

  colorSegmentationCB->setEnabled(thresholdOptionsVisible);
  updateColorReductionControls();
  // The check box first: the strength shown while it is off is only kept for turning it on.
  chromaCB->setChecked(colorCommonOptions.chromaDenoise());
  chromaOptionsWidget->setEnabled(colorCommonOptions.chromaDenoise());
  chromaRadius->setValue(colorCommonOptions.chromaRadius());
  chromaGrayLimit->setValue(colorCommonOptions.chromaGrayLimit());
  updateEnhancementFilter(subtractBgCB, subtractBgOptionsWidget, subtractBgCoef, subtractBgRadius,
                          colorCommonOptions.subtractBackgroundCoef(), m_subtractBgCoefWhenOn,
                          colorCommonOptions.subtractBackgroundRadius());
  subtractBgWhiteCB->setChecked(colorCommonOptions.subtractBackgroundWhite());
  updateEnhancementFilter(knnCB, knnOptionsWidget, knnCoef, knnRadius, colorCommonOptions.knnCoef(), m_knnCoefWhenOn,
                          colorCommonOptions.knnRadius());
  updateEnhancementFilter(sharpenCB, sharpenOptionsWidget, sharpenCoef, sharpenRadius, colorCommonOptions.sharpenCoef(),
                          m_sharpenCoefWhenOn, colorCommonOptions.sharpenRadius());
  updateEnhancementFilter(wienerCB, wienerOptionsWidget, wienerCoef, wienerWindowSize, colorCommonOptions.wienerCoef(),
                          m_wienerCoefWhenOn, colorCommonOptions.wienerWindowSize());
  colorSegmentationCB->setChecked(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  reduceNoiseSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getNoiseReduction());
  redAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getRedThresholdAdjustment());
  greenAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getGreenThresholdAdjustment());
  blueAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getBlueThresholdAdjustment());
  {
    const QSignalBlocker reductionBlocker(colorReductionCB);
    const QSignalBlocker colorsBlocker(kmeansColorsSB);
    const BlackWhiteOptions::ColorSegmenterOptions& segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
    colorReductionCB->setCurrentIndex(colorReduction());
    kmeansColorsSB->setValue(segmenterOptions.getKMeansColors());
    kmeansFixBlackCB->setChecked(segmenterOptions.isKMeansBlackFixed());
  }
  posterizeLevelSB->setValue(colorCommonOptions.getPosterizationOptions().getLevel());
  posterizeNormalizationCB->setChecked(colorCommonOptions.getPosterizationOptions().isNormalizationEnabled());
  posterizeForceBwCB->setChecked(colorCommonOptions.getPosterizationOptions().isForceBlackAndWhite());

  // Also shown, grayed out, in the modes that don't use them; only the stored values are displayed.
  {
    const QSignalBlocker shapeBlocker(pictureShapeSelector);
    const QSignalBlocker sensitivityBlocker(pictureShapeSensitivitySB);
    const QSignalBlocker higherBlocker(higherSearchSensitivityCB);
    const int pictureShapeIdx = pictureShapeSelector->findData(m_pictureShapeOptions.getPictureShape());
    pictureShapeSelector->setCurrentIndex(pictureShapeIdx);
    pictureShapeSensitivitySB->setValue(m_pictureShapeOptions.getSensitivity());
    pictureShapeSensitivityOptions->setEnabled(m_pictureShapeOptions.getPictureShape() == RECTANGULAR_SHAPE);
    higherSearchSensitivityCB->setChecked(m_pictureShapeOptions.isHigherSearchSensitivity());
    higherSearchSensitivityCB->setEnabled(m_pictureShapeOptions.getPictureShape() != OFF_SHAPE);
  }

  {
    const QSignalBlocker despeckleBlocker(despeckleCB);
    const QSignalBlocker sliderBlocker(despeckleSlider);
    if (m_despeckleLevel != 0) {
      despeckleCB->setChecked(true);
      despeckleSlider->setValue(qRound(10 * m_despeckleLevel));
    } else {
      despeckleCB->setChecked(false);
    }
    despeckleSlider->setEnabled(m_despeckleLevel != 0);
    despeckleValueLabel->setEnabled(m_despeckleLevel != 0);
    despeckleSlider->setToolTip(QLocale().toString(0.1 * despeckleSlider->value(), 'f', 1));
    despeckleValueLabel->setText(despeckleSlider->toolTip());
  }

  updateBinarizationWidgets();
}  // OptionsWidget::updateColorsDisplay

void OptionsWidget::updateDewarpingDisplay() {
  // Shown in every view, grayed out while there is no dewarping.
  const DewarpingMode mode = m_dewarpingOptions.dewarpingMode();
  dewarpingMethodBox->setEnabled(mode != OFF);
  dewarpingMethodLabel->setEnabled(mode != OFF);
  // Straight lines have no curvature for the depth perception to work on.
  depthPerceptionPanel->setEnabled((mode != OFF) && (mode != TRAPEZOID));
  evenOutLetterWidthsCB->setEnabled(mode != OFF);
  dewarpingNotFoundLabel->setVisible(m_dewarpingOptions.noModelFound() && ((mode == AUTO) || (mode == TRAPEZOID)));

  {
    const QSignalBlocker dewarpingBlocker(dewarpingCB);
    const QSignalBlocker postDeskewBlocker(dewarpingPostDeskewCB);
    const QSignalBlocker evenOutBlocker(evenOutLetterWidthsCB);
    dewarpingCB->setChecked(mode != OFF);
    if (mode != OFF) {
      // While dewarping is off, the list goes on showing the method used last.
      const QSignalBlocker methodBlocker(dewarpingMethodBox);
      DewarpingMethodBox::selectMethod(dewarpingMethodBox, mode);
    }
    dewarpingPostDeskewCB->setChecked(m_dewarpingOptions.needPostDeskew());
    evenOutLetterWidthsCB->setChecked(m_dewarpingOptions.evenOutLetterWidths());
  }

  // The angle found by the post deskew is shown where it is used.
  QString postDeskewText = tr("Post deskew");
  if (m_dewarpingOptions.needPostDeskew()
      && ((m_dewarpingOptions.dewarpingMode() == MANUAL) || (m_dewarpingOptions.dewarpingMode() == MARGINAL))) {
    double deskewAngle = -std::round(m_dewarpingOptions.getPostDeskewAngle() * 100) / 100;
    if (deskewAngle == 0.0) {
      // Not "-0" for a small positive angle.
      deskewAngle = 0.0;
    }
    postDeskewText += " (" + QString::number(deskewAngle) + QChar(0x00B0) + ")";
  }
  dewarpingPostDeskewCB->setText(postDeskewText);
  // Only Manual and Marginal deskew the page after dewarping.
  dewarpingPostDeskewCB->setEnabled((m_dewarpingOptions.dewarpingMode() == MANUAL)
                                    || (m_dewarpingOptions.dewarpingMode() == MARGINAL));

  {
    const QSignalBlocker sliderBlocker(depthPerceptionSlider);
    depthPerceptionSlider->setValue(qRound(m_depthPerception.value() * 10));
  }
  depthPerceptionValueLabel->setText(QLocale().toString(m_depthPerception.value(), 'f', 1));
}

void OptionsWidget::savitzkyGolaySmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setSavitzkyGolaySmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::morphologicalSmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setMorphologicalSmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::coloredDimmingChanged(const double value) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setColoredDimming(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::binarizationBoundChanged() {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setLowerBound(lowerBoundSB->value());
  opt.setUpperBound(upperBoundSB->value());
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::bwForegroundToggled(bool /*checked*/) {
  // Connected to clicked(), so the button is always checked here.
  m_splittingOptions.setSplittingMode(BLACK_AND_WHITE_FOREGROUND);
  updateOriginalBackgroundEnabled();
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  requestReloadNow();
}

void OptionsWidget::colorForegroundToggled(bool /*checked*/) {
  // Connected to clicked(), so the button is always checked here.
  m_splittingOptions.setSplittingMode(COLOR_FOREGROUND);
  updateOriginalBackgroundEnabled();
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  requestReloadNow();
}

void OptionsWidget::updateOriginalBackgroundEnabled() {
  originalBackgroundCB->setEnabled(m_splittingOptions.isSplitOutput()
                                   && (m_splittingOptions.getSplittingMode() == BLACK_AND_WHITE_FOREGROUND));
}

void OptionsWidget::splittingToggled(bool checked) {
  m_splittingOptions.setSplitOutput(checked);

  bwForegroundRB->setEnabled(checked);
  colorForegroundRB->setEnabled(checked);
  updateOriginalBackgroundEnabled();

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  requestReloadNow();
}

void OptionsWidget::originalBackgroundToggled(bool checked) {
  m_splittingOptions.setOriginalBackgroundEnabled(checked);

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  requestReloadNow();
}

void OptionsWidget::chromaToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaDenoise(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  chromaOptionsWidget->setEnabled(checked);
  requestReloadNow();
}

void OptionsWidget::chromaRadiusChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaRadius(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::chromaGrayLimitChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaGrayLimit(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::updateEnhancementFilter(QCheckBox* cb,
                                            QWidget* optionsWidget,
                                            QDoubleSpinBox* coefBox,
                                            QSpinBox* paramBox,
                                            const double coef,
                                            double& coefWhenOn,
                                            const int param) {
  const bool enabled = coef > 0.0;
  if (enabled) {
    coefWhenOn = coef;
  }
  cb->setChecked(enabled);
  optionsWidget->setEnabled(enabled);
  coefBox->setValue(coefWhenOn);
  paramBox->setValue(param);
}

void OptionsWidget::subtractBgWhiteToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSubtractBackgroundWhite(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  requestReloadNow();
}

void OptionsWidget::colorSegmentationToggled(bool checked) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setEnabled(checked);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  updateColorReductionControls();

  requestReloadNow();
}

void OptionsWidget::reduceNoiseChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setNoiseReduction(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::redAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setRedThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::greenAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setGreenThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::blueAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setBlueThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

bool OptionsWidget::reducesTextColors() const {
  const ColorMode colorMode = m_colorParams.colorMode();
  return (colorMode == BLACK_AND_WHITE) || (colorMode == MIXED);
}

int OptionsWidget::colorReduction() const {
  // The items of "Reduce colors": none, posterize, K-means. Black and white and mixed pages reduce
  // the colors of the text, color pages those of the whole page.
  if (reducesTextColors()) {
    const BlackWhiteOptions::ColorSegmenterOptions& segmenterOptions
        = m_colorParams.blackWhiteOptions().getColorSegmenterOptions();
    if (segmenterOptions.isKMeansEnabled()) {
      return 2;
    }
    return segmenterOptions.isPosterizeEnabled() ? 1 : 0;
  }
  return m_colorParams.colorCommonOptions().getPosterizationOptions().isEnabled() ? 1 : 0;
}

void OptionsWidget::updateColorReductionControls() {
  const bool textColors = reducesTextColors();
  const bool segmentation = textColors && m_colorParams.blackWhiteOptions().getColorSegmenterOptions().isEnabled();
  const int reduction = colorReduction();
  segmenterOptionsWidget->setEnabled(segmentation);

  // The text colors can only be reduced with color segmentation; K-means only reduces text colors.
  const bool listUsable = !textColors || segmentation;
  colorReductionLabel->setEnabled(listUsable);
  colorReductionCB->setEnabled(listUsable);
  if (auto* model = qobject_cast<QStandardItemModel*>(colorReductionCB->model())) {
    model->item(2)->setEnabled(textColors);
  }
  {
    const QSignalBlocker blocker(colorReductionCB);
    colorReductionCB->setCurrentIndex(reduction);
  }
  // Only the settings of the chosen method are shown, in the same place: one number and its check boxes.
  for (QWidget* widget : std::initializer_list<QWidget*>{kmeansColorsLabel, kmeansColorsSB, kmeansFixBlackCB}) {
    widget->setVisible(reduction == 2);
    widget->setEnabled(listUsable);
  }
  for (QWidget* widget : std::initializer_list<QWidget*>{posterizeLevelLabel, posterizeLevelSB,
                                                         posterizeNormalizationCB, posterizeForceBwCB}) {
    widget->setVisible(reduction == 1);
    widget->setEnabled(listUsable);
  }
}

void OptionsWidget::colorReductionChanged(const int index) {
  if (reducesTextColors()) {
    BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
    BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
    segmenterOptions.setPosterizeEnabled(index == 1);
    segmenterOptions.setKMeansEnabled(index == 2);
    blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
    m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  } else {
    ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
    ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
    posterizationOptions.setEnabled(index == 1);
    colorCommonOptions.setPosterizationOptions(posterizationOptions);
    m_colorParams.setColorCommonOptions(colorCommonOptions);
  }
  m_settings->setColorParams(m_pageId, m_colorParams);

  updateColorReductionControls();
  requestReloadNow();
}

void OptionsWidget::kmeansColorsChanged(const int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setKMeansColors(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::kmeansFixBlackToggled(const bool checked) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setKMeansBlackFixed(checked);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  requestReloadNow();
}

void OptionsWidget::posterizeLevelChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setLevel(value);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(RELOAD_DELAY_MS);
}

void OptionsWidget::posterizeNormalizationToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setNormalizationEnabled(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  requestReloadNow();
}

void OptionsWidget::posterizeForceBwToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setForceBlackAndWhite(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  requestReloadNow();
}

void OptionsWidget::updateBinarizationOptionsDisplay(int idx) {
  for (int i = 0; i < binarizationOptions->count(); i++) {
    QWidget* currentWidget = binarizationOptions->widget(i);
    currentWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    currentWidget->resize(0, 0);

    disconnect(static_cast<OptionsWidgetBinarization*>(currentWidget), &OptionsWidgetBinarization::stateChanged, this,
               &OptionsWidget::binarizationSettingsChanged);
  }

  QWidget* widget = binarizationOptions->widget(idx);
  if (widget == nullptr) {
    return;
  }
  widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  widget->adjustSize();
  binarizationOptions->adjustSize();

  connect(static_cast<OptionsWidgetBinarization*>(widget), &OptionsWidgetBinarization::stateChanged, this,
          &OptionsWidget::binarizationSettingsChanged);
}

void OptionsWidget::selectBinarizationPanel(const BinarizationMethod method) {
  for (int i = 0; i < binarizationOptions->count(); ++i) {
    if (static_cast<OptionsWidgetBinarization*>(binarizationOptions->widget(i))->method() == method) {
      binarizationOptions->setCurrentIndex(i);
      return;
    }
  }
}

void OptionsWidget::requestReloadNow() {
  m_delayedReloadRequest.stop();
  emit reloadRequested();
}

ApplyParamsDialog* OptionsWidget::createApplyDialog(const QString& title) {
  auto* dialog = new ApplyParamsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  if (!title.isEmpty()) {
    dialog->setWindowTitle(title);
  }
  dialog->show();
  return dialog;
}

template <typename OnAccepted>
ApplyParamsDialog* OptionsWidget::openApplyDialog(const QString& title, OnAccepted&& onAccepted) {
  ApplyParamsDialog* dialog = createApplyDialog(title);
  connect(dialog, &ApplyParamsDialog::accepted, this, std::forward<OnAccepted>(onAccepted));
  return dialog;
}

void OptionsWidget::addOptionsWidgetBinarization(OptionsWidgetBinarization* widget) {
  widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  binarizationOptions->addWidget(widget);
  connect(widget, &OptionsWidgetBinarization::colorParamsChanged, this, &OptionsWidget::binarizationColorParamsChanged);
}

void OptionsWidget::updateBinarizationWidgets() {
  // Only OptionsWidgetBinarization objects are added, see addOptionsWidgetBinarization().
  for (int i = 0; i < binarizationOptions->count(); i++) {
    static_cast<OptionsWidgetBinarization*>(binarizationOptions->widget(i))->updateUi(m_pageId);
  }
}

void OptionsWidget::sendReloadRequested() {
  emit reloadRequested();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

template <typename GetCoef, typename SetCoef, typename SetParam>
void OptionsWidget::connectEnhancementFilter(QCheckBox* cb,
                                             QWidget* optionsWidget,
                                             QDoubleSpinBox* coefBox,
                                             QSpinBox* paramBox,
                                             double& coefWhenOn,
                                             GetCoef getCoef,
                                             SetCoef setCoef,
                                             SetParam setParam) {
  (void) getCoef;
  CONNECT(cb, &QAbstractButton::clicked, this, [this, optionsWidget, &coefWhenOn, setCoef](const bool checked) {
    ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
    (colorCommonOptions.*setCoef)(checked ? coefWhenOn : 0.0);
    m_colorParams.setColorCommonOptions(colorCommonOptions);
    m_settings->setColorParams(m_pageId, m_colorParams);

    optionsWidget->setEnabled(checked);
    requestReloadNow();
  });
  CONNECT(coefBox, &QDoubleSpinBox::valueChanged, this, [this, cb, &coefWhenOn, setCoef](const double value) {
    if (!cb->isChecked()) {
      return;
    }
    coefWhenOn = value;
    ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
    (colorCommonOptions.*setCoef)(value);
    m_colorParams.setColorCommonOptions(colorCommonOptions);
    m_settings->setColorParams(m_pageId, m_colorParams);

    m_delayedReloadRequest.start(RELOAD_DELAY_MS);
  });
  CONNECT(paramBox, &QSpinBox::valueChanged, this, [this, setParam](const int value) {
    ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
    (colorCommonOptions.*setParam)(value);
    m_colorParams.setColorCommonOptions(colorCommonOptions);
    m_settings->setColorParams(m_pageId, m_colorParams);

    m_delayedReloadRequest.start(RELOAD_DELAY_MS);
  });
}

void OptionsWidget::setupUiConnections() {
  CONNECT(dpiSelector, &QComboBox::activated, this, &OptionsWidget::dpiSelectionChanged);
  CONNECT(dpiSelector->lineEdit(), &QLineEdit::editingFinished, this, &OptionsWidget::dpiSelectionChanged);
  CONNECT(applyDpiButton, &QAbstractButton::clicked, this, &OptionsWidget::applyDpiButtonClicked);
  CONNECT(colorModeBWBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(BLACK_AND_WHITE); });
  CONNECT(colorModeColorBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(COLOR_GRAYSCALE); });
  CONNECT(colorModeMixedBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(MIXED); });
  CONNECT(colorDetectionBtn, &QAbstractButton::clicked, this, &OptionsWidget::colorDetectionButtonClicked);
  CONNECT(colorDetectionMinColorSB, &QSpinBox::valueChanged, this, &OptionsWidget::colorDetectionMinColorChanged);
  CONNECT(colorDetectionZonesCB, &QAbstractButton::clicked, this, &OptionsWidget::colorDetectionZonesToggled);
  CONNECT(colorDetectionMinPictureSB, &QSpinBox::valueChanged, this, &OptionsWidget::colorDetectionMinPictureChanged);
  CONNECT(thresholdMethodBox, &QComboBox::currentIndexChanged, this, &OptionsWidget::thresholdMethodChanged);
  CONNECT(fillingColorBox, &QComboBox::currentIndexChanged, this, &OptionsWidget::fillingColorChanged);
  CONNECT(pictureShapeSelector, &QComboBox::currentIndexChanged, this, &OptionsWidget::pictureShapeChanged);
  CONNECT(pictureShapeSensitivitySB, &QSpinBox::valueChanged, this, &OptionsWidget::pictureShapeSensitivityChanged);
  CONNECT(higherSearchSensitivityCB, &QAbstractButton::clicked, this, &OptionsWidget::higherSearchSensivityToggled);

  CONNECT(chromaCB, &QAbstractButton::clicked, this, &OptionsWidget::chromaToggled);
  CONNECT(chromaRadius, &QSpinBox::valueChanged, this, &OptionsWidget::chromaRadiusChanged);
  CONNECT(chromaGrayLimit, &QSpinBox::valueChanged, this, &OptionsWidget::chromaGrayLimitChanged);
  connectEnhancementFilter(subtractBgCB, subtractBgOptionsWidget, subtractBgCoef, subtractBgRadius,
                           m_subtractBgCoefWhenOn, &ColorCommonOptions::subtractBackgroundCoef,
                           &ColorCommonOptions::setSubtractBackgroundCoef,
                           &ColorCommonOptions::setSubtractBackgroundRadius);
  CONNECT(subtractBgWhiteCB, &QAbstractButton::clicked, this, &OptionsWidget::subtractBgWhiteToggled);
  connectEnhancementFilter(knnCB, knnOptionsWidget, knnCoef, knnRadius, m_knnCoefWhenOn, &ColorCommonOptions::knnCoef,
                           &ColorCommonOptions::setKnnCoef, &ColorCommonOptions::setKnnRadius);
  connectEnhancementFilter(sharpenCB, sharpenOptionsWidget, sharpenCoef, sharpenRadius, m_sharpenCoefWhenOn,
                           &ColorCommonOptions::sharpenCoef, &ColorCommonOptions::setSharpenCoef,
                           &ColorCommonOptions::setSharpenRadius);
  connectEnhancementFilter(wienerCB, wienerOptionsWidget, wienerCoef, wienerWindowSize, m_wienerCoefWhenOn,
                           &ColorCommonOptions::wienerCoef, &ColorCommonOptions::setWienerCoef,
                           &ColorCommonOptions::setWienerWindowSize);
  CONNECT(colorSegmentationCB, &QAbstractButton::clicked, this, &OptionsWidget::colorSegmentationToggled);
  CONNECT(reduceNoiseSB, &QSpinBox::valueChanged, this, &OptionsWidget::reduceNoiseChanged);
  CONNECT(colorReductionCB, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &OptionsWidget::colorReductionChanged);
  CONNECT(kmeansColorsSB, &QSpinBox::valueChanged, this, &OptionsWidget::kmeansColorsChanged);
  CONNECT(kmeansFixBlackCB, &QAbstractButton::clicked, this, &OptionsWidget::kmeansFixBlackToggled);
  CONNECT(redAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::redAdjustmentChanged);
  CONNECT(greenAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::greenAdjustmentChanged);
  CONNECT(blueAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::blueAdjustmentChanged);
  CONNECT(posterizeLevelSB, &QSpinBox::valueChanged, this, &OptionsWidget::posterizeLevelChanged);
  CONNECT(posterizeNormalizationCB, &QAbstractButton::clicked, this, &OptionsWidget::posterizeNormalizationToggled);
  CONNECT(posterizeForceBwCB, &QAbstractButton::clicked, this, &OptionsWidget::posterizeForceBwToggled);

  CONNECT(fillMarginsCB, &QAbstractButton::clicked, this, &OptionsWidget::fillMarginsToggled);
  CONNECT(fillOffcutCB, &QAbstractButton::clicked, this, &OptionsWidget::fillOffcutToggled);
  CONNECT(fillOutsidePageBoxCB, &QAbstractButton::clicked, this, &OptionsWidget::fillOutsidePageBoxToggled);
  CONNECT(equalizeIlluminationCB, &QAbstractButton::clicked, this, &OptionsWidget::equalizeIlluminationToggled);
  CONNECT(equalizeIlluminationColorCB, &QAbstractButton::clicked, this,
          &OptionsWidget::equalizeIlluminationColorToggled);
  CONNECT(grayscaleOutputCB, &QAbstractButton::clicked, this, &OptionsWidget::grayscaleOutputToggled);
  CONNECT(savitzkyGolaySmoothingCB, &QAbstractButton::clicked, this, &OptionsWidget::savitzkyGolaySmoothingToggled);
  CONNECT(morphologicalSmoothingCB, &QAbstractButton::clicked, this, &OptionsWidget::morphologicalSmoothingToggled);
  CONNECT(coloredDimmingSB, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::coloredDimmingChanged);
  CONNECT(lowerBoundSB, &QSpinBox::valueChanged, this, &OptionsWidget::binarizationBoundChanged);
  CONNECT(upperBoundSB, &QSpinBox::valueChanged, this, &OptionsWidget::binarizationBoundChanged);
  CONNECT(splittingCB, &QAbstractButton::clicked, this, &OptionsWidget::splittingToggled);
  CONNECT(bwForegroundRB, &QAbstractButton::clicked, this, &OptionsWidget::bwForegroundToggled);
  CONNECT(colorForegroundRB, &QAbstractButton::clicked, this, &OptionsWidget::colorForegroundToggled);
  CONNECT(originalBackgroundCB, &QAbstractButton::clicked, this, &OptionsWidget::originalBackgroundToggled);
  CONNECT(applyColorsButton, &QAbstractButton::clicked, this, &OptionsWidget::applyColorsButtonClicked);

  CONNECT(applySplittingButton, &QAbstractButton::clicked, this, &OptionsWidget::applySplittingButtonClicked);

  CONNECT(dewarpingCB, &QAbstractButton::clicked, this, &OptionsWidget::dewarpingToggled);
  CONNECT(dewarpingMethodBox, &QComboBox::activated, this, &OptionsWidget::dewarpingMethodActivated);
  CONNECT(dewarpingPostDeskewCB, &QAbstractButton::clicked, this, &OptionsWidget::dewarpingPostDeskewToggled);
  CONNECT(evenOutLetterWidthsCB, &QAbstractButton::clicked, this, &OptionsWidget::evenOutLetterWidthsToggled);
  CONNECT(applyDewarpingButton, &QAbstractButton::clicked, this, &OptionsWidget::applyDewarpingButtonClicked);


  CONNECT(despeckleCB, &QAbstractButton::clicked, this, &OptionsWidget::despeckleToggled);
  CONNECT(despeckleSlider, &QAbstractSlider::sliderReleased, this, &OptionsWidget::despeckleSliderReleased);
  CONNECT(despeckleSlider, &QAbstractSlider::valueChanged, this, &OptionsWidget::despeckleSliderValueChanged);
  CONNECT(applyDespeckleButton, &QAbstractButton::clicked, this, &OptionsWidget::applyDespeckleButtonClicked);
  CONNECT(depthPerceptionSlider, &QAbstractSlider::valueChanged, this, &OptionsWidget::depthPerceptionChangedSlot);
  CONNECT(depthPerceptionSlider, &QAbstractSlider::sliderReleased, this, &OptionsWidget::depthPerceptionSliderReleased);
  CONNECT(&m_delayedReloadRequest, &QTimer::timeout, this, &OptionsWidget::sendReloadRequested);

  m_photoAdjustmentsControls->setupConnections(
      m_connectionManager, [this](const PhotoAdjustments& adjustments) { commitPhotoAdjustments(adjustments); });
  CONNECT(applyPhotoAdjustmentsButton, &QAbstractButton::clicked, this,
          &OptionsWidget::applyPhotoAdjustmentsButtonClicked);

  CONNECT(blackOnWhiteCB, &QAbstractButton::clicked, this, &OptionsWidget::blackOnWhiteToggled);
  CONNECT(applyProcessingOptionsButton, &QAbstractButton::clicked, this, &OptionsWidget::applyProcessingParamsClicked);
}

#undef CONNECT

ImageViewTab OptionsWidget::lastTab() const {
  return m_lastTab->get();
}

const DepthPerception& OptionsWidget::depthPerception() const {
  return m_depthPerception;
}

void OptionsWidget::blackOnWhiteToggled(bool value) {
  m_settings->setBlackOnWhite(m_pageId, value);
  OutputProcessingParams processingParams = m_settings->getOutputProcessingParams(m_pageId);
  processingParams.setBlackOnWhiteSetManually(true);
  m_settings->setOutputProcessingParams(m_pageId, processingParams);

  requestReloadNow();
}

void OptionsWidget::applyProcessingParamsClicked() {
  openApplyDialog(tr("Apply Content"), &OptionsWidget::applyProcessingParamsConfirmed);
}

void OptionsWidget::applyProcessingParamsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setBlackOnWhite(pageId, m_settings->getParams(m_pageId).isBlackOnWhite());
    OutputProcessingParams processingParams = m_settings->getOutputProcessingParams(pageId);
    processingParams.setBlackOnWhiteSetManually(true);
    m_settings->setOutputProcessingParams(pageId, processingParams);
  }

  pagesChanged(pages);
}

void OptionsWidget::updateProcessingDisplay() {
  blackOnWhiteCB->setChecked(m_settings->getParams(m_pageId).isBlackOnWhite());
}
}  // namespace output
