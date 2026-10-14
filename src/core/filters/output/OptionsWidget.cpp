// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <core/ApplicationSettings.h>
#include <tiff.h>

#include <QButtonGroup>
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
#include <QToolTip>
#include <algorithm>
#include <utility>

#include "../../SectionHeading.h"
#include "../../Utils.h"
#include "ApplyColorsDialog.h"
#include "BinarizationMethods.h"
#include "FillZoneComparator.h"
#include "OptionsWidgetBinarizationOtsu.h"
#include "OptionsWidgetBinarizationSauvola.h"
#include "OptionsWidgetBinarizationWolf.h"
#include "PictureZoneComparator.h"

using namespace core;

namespace output {
OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_despeckleLevel(1.0),
      m_lastTab(TAB_OUTPUT),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  // Percent fields are as wide as "1000 %", as in the step "Margins".
  for (QSpinBox* spinBox : {colorDetectionMinColorSB, colorDetectionMinPictureSB, pictureShapeSensitivitySB}) {
    core::Utils::setNumberFieldWidth(spinBox, 1000);
  }
  // Pixel fields of the filters are as wide as "999 px", fields without a unit as "-100".
  for (QSpinBox* spinBox : {chromaRadius, subtractBgRadius, knnRadius, wienerWindowSize, sharpenRadius}) {
    core::Utils::setNumberFieldWidth(spinBox, 999);
  }
  for (QAbstractSpinBox* spinBox : std::initializer_list<QAbstractSpinBox*>{
           chromaGrayLimit, subtractBgCoef, knnCoef, wienerCoef, sharpenCoef, redAdjustmentSB, greenAdjustmentSB,
           blueAdjustmentSB, reduceNoiseSB, kmeansColorsSB, posterizeLevelSB}) {
    core::Utils::setNumberFieldWidth(spinBox, -100, 0);
  }
  // As wide as the other fields without a unit.
  for (QAbstractSpinBox* spinBox :
       std::initializer_list<QAbstractSpinBox*>{coloredDimmingSB, lowerBoundSB, upperBoundSB}) {
    core::Utils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_delayedReloadRequest.setSingleShot(true);

  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(0.5 * 10));
  despeckleSlider->setMaximum(qRound(3.5 * 10));

#ifndef ENABLE_OCR
  // The color mode detection comes with OCR (Leptonica).
  colorDetectionPanel->setVisible(false);
#endif

  fillBinarizationMethodBox(thresholdMethodBox);

  fillingColorBox->addItem(tr("Background"), FILL_BACKGROUND);
  fillingColorBox->addItem(tr("White"), FILL_WHITE);
  fillingColorBox->addItem(tr("Black"), FILL_BLACK);

  QPointer<OptionsWidgetBinarization> otsuOptionsWidgetBinarization = new OptionsWidgetBinarizationOtsu(m_settings);
  QPointer<OptionsWidgetBinarization> sauvolaOptionsWidgetBinarization
      = new OptionsWidgetBinarizationSauvola(m_settings);
  QPointer<OptionsWidgetBinarization> wolfOptionsWidgetBinarization = new OptionsWidgetBinarizationWolf(m_settings);
  QPointer<OptionsWidgetBinarization> foxOptionsWidgetBinarization = new OptionsWidgetBinarizationWolf(m_settings);
  QPointer<OptionsWidgetBinarization> windowOptionsWidgetBinarization = new OptionsWidgetBinarizationWolf(m_settings);
  QPointer<OptionsWidgetBinarization> bradleyOptionsWidgetBinarization
      = new OptionsWidgetBinarizationSauvola(m_settings);
  QPointer<OptionsWidgetBinarization> gradOptionsWidgetBinarization = new OptionsWidgetBinarizationWolf(m_settings);
  QPointer<OptionsWidgetBinarization> edgeplusOptionsWidgetBinarization
      = new OptionsWidgetBinarizationSauvola(m_settings);
  QPointer<OptionsWidgetBinarization> blurdivOptionsWidgetBinarization
      = new OptionsWidgetBinarizationSauvola(m_settings);
  QPointer<OptionsWidgetBinarization> edgedivOptionsWidgetBinarization
      = new OptionsWidgetBinarizationSauvola(m_settings);

  while (binarizationOptions->count() != 0) {
    binarizationOptions->removeWidget(binarizationOptions->widget(0));
  }
  addOptionsWidgetBinarization(otsuOptionsWidgetBinarization);
  addOptionsWidgetBinarization(sauvolaOptionsWidgetBinarization);
  addOptionsWidgetBinarization(wolfOptionsWidgetBinarization);
  addOptionsWidgetBinarization(foxOptionsWidgetBinarization);
  addOptionsWidgetBinarization(windowOptionsWidgetBinarization);
  addOptionsWidgetBinarization(bradleyOptionsWidgetBinarization);
  addOptionsWidgetBinarization(gradOptionsWidgetBinarization);
  addOptionsWidgetBinarization(edgeplusOptionsWidgetBinarization);
  addOptionsWidgetBinarization(blurdivOptionsWidgetBinarization);
  addOptionsWidgetBinarization(edgedivOptionsWidgetBinarization);
  // The methods from ScanTailor Experimental have a window size and strength, as Sauvola.
  for (int i = T_GATOS; i <= T_MULTISCALE; ++i) {
    addOptionsWidgetBinarization(new OptionsWidgetBinarizationSauvola(m_settings));
  }
  // "Colored" and "Bounds" apply to all methods and line up with the fields of the method shown.
  QList<QGridLayout*> binarizationGrids = binarizationOptions->findChildren<QGridLayout*>();
  binarizationGrids.append(allMethodsLayout);
  core::Utils::alignLabelColumns(binarizationGrids);
  // The fields of the enhancement filters start at the same place; column 0 is the indent.
  core::Utils::alignLabelColumns(
      {chromaOptionsLayout, subtractBgOptionsLayout, knnOptionsLayout, wienerOptionsLayout, sharpenOptionsLayout}, 1);
  // The settings of the threshold method are indented below the method list, as the settings of
  // the enhancement filters below their check boxes (a 15 px indent column plus the spacing).
  const int methodIndent = 15 + chromaOptionsLayout->horizontalSpacing();
  binarizationOptions->setContentsMargins(methodIndent, 0, 0, 0);
  allMethodsLayout->setContentsMargins(methodIndent, 0, 0, 0);
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
    dpiSelector->addItem(Utils::dpiText(dpi));
  }
  dpiSelector->setValidator(Utils::createDpiValidator(dpiSelector));
  Utils::setDpiFieldWidth(dpiSelector);

  m_dewarpingModeGroup = new QButtonGroup(this);
  m_dewarpingModeGroup->addButton(dewarpingOffBtn, OFF);
  m_dewarpingModeGroup->addButton(dewarpingAutoBtn, AUTO);
  m_dewarpingModeGroup->addButton(dewarpingMarginalBtn, MARGINAL);
  m_dewarpingModeGroup->addButton(dewarpingManualBtn, MANUAL);

  setupPhotoAdjustmentsPanel();

  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();

  connect(binarizationOptions, &QStackedWidget::currentChanged, this, &OptionsWidget::updateBinarizationOptionsDisplay);

  setupTiffCompressionPanel();
  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::setupTiffCompressionPanel() {
  const ApplicationSettings& settings = ApplicationSettings::getInstance();

  // Built like the compression panel of the PDF step.  Unlike the other panels of this
  // step, it applies to all pages and projects, so there is no "Apply to ..." button.
  auto* group = new CollapsibleGroupBox(tr("TIFF Compression"));
  group->setObjectName("tiffCompressionPanel");
  group->setToolTip(
      tr("Compression of the TIFF files in the output folder.  Applies to all projects.  The PDF is compressed "
         "separately, so this doesn't change it.  After a change, the output of this project is created again: the "
         "current page right away, the other pages during the next batch processing.  Other projects keep their "
         "files until their pages are processed again."));
  auto* layout = new QFormLayout(group);
  // The lists keep their width instead of filling a widened panel, as in the PDF step.
  layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
  layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  // A part for each kind of page, each with a bold heading.
  auto addHeading = [layout](const QString& text, const QString& toolTip, const bool first) {
    auto* heading = new SectionHeading(text);
    heading->setToolTip(toolTip);
    heading->setFirst(first);
    layout->addRow(heading);
  };
  // A stored value that isn't offered falls back to the default, as the writer would get it.
  auto selectData = [](QComboBox* box, const int value, const int defaultValue) {
    int index = box->findData(value);
    if (index < 0) {
      index = box->findData(defaultValue);
    }
    box->setCurrentIndex(std::max(0, index));
  };
  const QString lzmaNote = tr(
      "LZMA makes the smallest lossless files, but is slow, and few other programs can read it; this program "
      "and its PDF export can.");

  // Color and grayscale.
  addHeading(tr("Color and Grayscale"),
             tr("Color and grayscale pages, also posterized grayscale pages, and the pictures of pages with split "
                "output."),
             true);
  m_tiffColorCompression = new QComboBox;
  m_tiffColorCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_tiffColorCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_tiffColorCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_tiffColorCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  m_tiffColorCompression->addItem(QStringLiteral("JPEG"), COMPRESSION_JPEG);
  selectData(m_tiffColorCompression, settings.getTiffColorCompression(), COMPRESSION_LZW);
  m_tiffColorCompression->setToolTip(
      tr("All methods but JPEG are lossless; Deflate usually gives smaller files than LZW.") + QStringLiteral("  ")
      + lzmaNote + QStringLiteral("  ")
      + tr("JPEG gives much smaller files, but loses quality: the PDF compresses the pictures a second time, and "
           "on mixed pages without split output the text gets blurred."));
  // The fields start right after the longest label of the group, the same for all parts.
  layout->addRow(tr("Method:"), m_tiffColorCompression);

  m_tiffJpegQuality = new QSpinBox;
  m_tiffJpegQuality->setRange(10, 100);
  core::Utils::setNumberFieldWidth(m_tiffJpegQuality, -100, 0);
  m_tiffJpegQuality->setValue(settings.getTiffJpegQuality());
  m_tiffJpegQuality->setToolTip(
      tr("Higher values give better pictures and larger files.  Only used with JPEG compression."));
  layout->addRow(tr("Quality:"), m_tiffJpegQuality);

  // Posterized pages.
  addHeading(tr("Posterized Pages"),
             tr("Color pages that were posterized, and posterized pictures of pages with split output.  Posterized "
                "grayscale pages can't be told apart from other grayscale pages; they follow \"Color and "
                "Grayscale\"."),
             false);
  m_tiffPaletteCompression = new QComboBox;
  m_tiffPaletteCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_tiffPaletteCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_tiffPaletteCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_tiffPaletteCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  selectData(m_tiffPaletteCompression, settings.getTiffPaletteCompression(), COMPRESSION_LZW);
  m_tiffPaletteCompression->setToolTip(tr("All methods are lossless.") + QStringLiteral("  ") + lzmaNote);
  layout->addRow(tr("Method:"), m_tiffPaletteCompression);

  // Black and white.
  addHeading(tr("Black and White"), tr("Black and white pages and the text of pages with split output."), false);
  m_tiffBwCompression = new QComboBox;
  m_tiffBwCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_tiffBwCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_tiffBwCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_tiffBwCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  m_tiffBwCompression->addItem(QStringLiteral("CCITT G4"), COMPRESSION_CCITTFAX4);
  selectData(m_tiffBwCompression, settings.getTiffBwCompression(), COMPRESSION_CCITTFAX4);
  m_tiffBwCompression->setToolTip(tr("All methods are lossless; CCITT G4 usually gives the smallest files.")
                                  + QStringLiteral("  ") + lzmaNote);
  layout->addRow(tr("Method:"), m_tiffBwCompression);

  // The lists of the three parts line up.
  int listWidth = 0;
  for (QComboBox* box : {m_tiffColorCompression, m_tiffPaletteCompression, m_tiffBwCompression}) {
    listWidth = std::max(listWidth, box->sizeHint().width());
  }
  for (QComboBox* box : {m_tiffColorCompression, m_tiffPaletteCompression, m_tiffBwCompression}) {
    box->setFixedWidth(listWidth);
  }

  m_tiffJpegQuality->setEnabled(m_tiffColorCompression->currentData().toInt() == COMPRESSION_JPEG);

  // At the bottom, above the spacer that pushes the panels up.
  verticalLayout_3->insertWidget(verticalLayout_3->count() - 1, group);

  connect(m_tiffColorCompression, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this]() { tiffCompressionChanged(false); });
  connect(m_tiffJpegQuality, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { tiffCompressionChanged(true); });
  connect(m_tiffPaletteCompression, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this]() { tiffCompressionChanged(false); });
  connect(m_tiffBwCompression, qOverload<int>(&QComboBox::currentIndexChanged), this,
          [this]() { tiffCompressionChanged(false); });
}

void OptionsWidget::tiffCompressionChanged(const bool delayReload) {
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  const int color = m_tiffColorCompression->currentData().toInt();
  const int quality = m_tiffJpegQuality->value();
  const int palette = m_tiffPaletteCompression->currentData().toInt();
  const int bw = m_tiffBwCompression->currentData().toInt();

  const bool changed = (color != settings.getTiffColorCompression())
                       || (palette != settings.getTiffPaletteCompression()) || (bw != settings.getTiffBwCompression())
                       || ((color == COMPRESSION_JPEG) && (quality != settings.getTiffJpegQuality()));
  settings.setTiffColorCompression(color);
  settings.setTiffJpegQuality(quality);
  settings.setTiffPaletteCompression(palette);
  settings.setTiffBwCompression(bw);
  m_tiffJpegQuality->setEnabled(color == COMPRESSION_JPEG);
  if (!changed) {
    return;
  }

  // The existing output files keep their compression unless they are written again.
  m_settings->removeAllOutputParams();
  emit invalidateAllThumbnails();
  if (delayReload) {
    // Don't recreate the page for every step of the spin box.
    m_delayedReloadRequest.start(750);
  } else {
    emit reloadRequested();
  }
}

void OptionsWidget::preUpdateUI(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  const Params params = m_settings->getParams(pageId);
  m_pageId = pageId;
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
  m_lastTab = tab;
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
  emit reloadRequested();
}

void OptionsWidget::colorDetectionMinColorChanged(const int percent) {
  ApplicationSettings::getInstance().setColorDetectionMinColor(percent);
}

void OptionsWidget::colorDetectionZonesToggled(const bool checked) {
  ApplicationSettings::getInstance().setColorDetectionZones(checked);
  colorDetectionMinPictureLabel->setEnabled(checked);
  colorDetectionMinPictureSB->setEnabled(checked);
}

void OptionsWidget::colorDetectionMinPictureChanged(const int percent) {
  ApplicationSettings::getInstance().setColorDetectionMinPicture(percent);
}

void OptionsWidget::colorDetectionButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Detect Color Mode"));
  dialog->setThisPageIncluded(true);
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::colorModeDetectionRequested);
  dialog->show();
}

void OptionsWidget::thresholdMethodChanged(int idx) {
  const BinarizationMethod method = (BinarizationMethod) thresholdMethodBox->itemData(idx).toInt();
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  const bool ownDefaults = needsOwnBinarizationDefaults(blackWhiteOptions.getBinarizationMethod(), method);
  blackWhiteOptions.setBinarizationMethod(method);
  if (ownDefaults) {
    applyBinarizationDefaults(blackWhiteOptions);
  }
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  if (ownDefaults) {
    // Show the new window size and strength.
    for (int i = 0; i < binarizationOptions->count(); i++) {
      static_cast<OptionsWidgetBinarization*>(binarizationOptions->widget(i))->updateUi(m_pageId);
    }
  }

  emit reloadRequested();
}

void OptionsWidget::fillingColorChanged(int idx) {
  const FillingColor color = (FillingColor) fillingColorBox->itemData(idx).toInt();
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillingColor(color);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
}

void OptionsWidget::pictureShapeChanged(const int idx) {
  const auto shapeMode = static_cast<PictureShape>(pictureShapeSelector->itemData(idx).toInt());
  m_pictureShapeOptions.setPictureShape(shapeMode);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  pictureShapeSensitivityOptions->setEnabled(shapeMode == RECTANGULAR_SHAPE);
  higherSearchSensitivityCB->setEnabled(shapeMode != OFF_SHAPE);

  emit reloadRequested();
}

void OptionsWidget::pictureShapeSensitivityChanged(int value) {
  m_pictureShapeOptions.setSensitivity(value);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::higherSearchSensivityToggled(const bool checked) {
  m_pictureShapeOptions.setHigherSearchSensitivity(checked);
  m_settings->setPictureShapeOptions(m_pageId, m_pictureShapeOptions);

  emit reloadRequested();
}

void OptionsWidget::fillMarginsToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillMargins(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::fillOffcutToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillOffcut(checked);
  if (checked) {
    colorCommonOptions.setFillOutsidePageBox(false);
    fillOutsidePageBoxCB->blockSignals(true);
    fillOutsidePageBoxCB->setChecked(false);
    fillOutsidePageBoxCB->blockSignals(false);
  }
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::fillOutsidePageBoxToggled(const bool checked) {
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  colorCommonOptions.setFillOutsidePageBox(checked);
  if (checked) {
    colorCommonOptions.setFillOffcut(false);
    fillOffcutCB->blockSignals(true);
    fillOffcutCB->setChecked(false);
    fillOffcutCB->blockSignals(false);
  }
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
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
      if (!checked) {
        ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
        colorCommonOptions.setNormalizeIllumination(false);
        equalizeIlluminationColorCB->setChecked(false);
        m_colorParams.setColorCommonOptions(colorCommonOptions);
      }
      equalizeIlluminationColorCB->setEnabled(checked);
    }
  }

  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::equalizeIlluminationColorToggled(const bool checked) {
  ColorCommonOptions opt(m_colorParams.colorCommonOptions());
  opt.setNormalizeIllumination(checked);
  m_colorParams.setColorCommonOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::grayscaleOutputToggled(const bool checked) {
  ColorCommonOptions opt(m_colorParams.colorCommonOptions());
  opt.setGrayscaleOutput(checked);
  m_colorParams.setColorCommonOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
  emit invalidateThumbnail(m_pageId);
}


void OptionsWidget::binarizationSettingsChanged() {
  emit reloadRequested();
  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::dpiSelectionChanged() {
  // The message box takes the focus, which finishes the editing a second time.
  if (m_checkingDpi) {
    return;
  }
  bool ok = false;
  const int dpi = Utils::dpiFromText(dpiSelector->currentText(), &ok);
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
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Output Resolution"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applyDpiConfirmed);
  dialog->show();
}

void OptionsWidget::applyDpiConfirmed(const std::set<PageId>& pages) {
  dpiChanged(pages, m_outputDpi);
}

void OptionsWidget::applyColorsButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->showModeParts();
  connect(dialog, &ApplyColorsDialog::accepted, this, [this, dialog](const std::set<PageId>& pages) {
    applyColorsConfirmed(pages, dialog->isColorModeChosen(), dialog->isOtherSettingsChosen());
  });
  dialog->show();
}

void OptionsWidget::dpiChanged(const std::set<PageId>& pages, const Dpi& dpi) {
  for (const PageId& pageId : pages) {
    Params params(m_settings->getParams(pageId));
    // Keeps the curves found automatically, so they are not searched again at the new resolution.
    // Without curves found yet, "Manual" would mean no dewarping at all, so the page stays on
    // "Auto".
    if ((params.dewarpingOptions().dewarpingMode() == AUTO) && params.distortionModel().isValid()) {
      DewarpingOptions opt(params.dewarpingOptions());
      opt.setDewarpingMode(MANUAL);
      m_settings->setDewarpingOptions(pageId, opt);
    }
    m_settings->setDpi(pageId, dpi);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    m_outputDpi = dpi;
    updateDpiDisplay();
    emit reloadRequested();
  }
}

void OptionsWidget::applyColorsConfirmed(const std::set<PageId>& pages,
                                         const bool applyColorMode,
                                         const bool applyOtherSettings) {
  for (const PageId& pageId : pages) {
    // The color mode and the other settings of the Mode group can be applied separately.
    ColorParams colorParams = applyOtherSettings ? m_colorParams : m_settings->getParams(pageId).colorParams();
    // The photo adjustments have their own "Apply to ...".
    colorParams.setPhotoAdjustments(m_settings->getParams(pageId).colorParams().photoAdjustments());
    if (applyColorMode) {
      colorParams.setColorMode(m_colorParams.colorMode());
    } else {
      colorParams.setColorMode(m_settings->getParams(pageId).colorParams().colorMode());
    }
    m_settings->setColorParams(pageId, colorParams);
    if (applyOtherSettings) {
      m_settings->setPictureShapeOptions(pageId, m_pictureShapeOptions);
    }
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::setupPhotoAdjustmentsPanel() {
  struct RowText {
    QString label;
    QString toolTip;
  };
  const RowText texts[PHOTO_ADJUSTMENT_COUNT]
      = {{tr("Temperature:"),
          tr("Positive values make the pictures warmer (more yellow), negative values cooler (more blue).")},
         {tr("Tint:"), tr("Positive values add magenta, negative values green.")},
         {tr("Exposure:"), tr("Brightens or darkens the pictures as a whole, in stops (EV).")},
         {tr("Contrast:"), tr("Positive values spread the tones apart, negative values bring them closer together, "
                              "around the middle gray.")},
         {tr("Highlights:"), tr("Positive values brighten the bright parts, negative values darken them.")},
         {tr("Shadows:"), tr("Positive values brighten the dark parts, negative values darken them.")},
         {tr("Whites:"), tr("Positive values make the brightest tones whiter, negative values duller.")},
         {tr("Blacks:"), tr("Positive values make the darkest tones lighter, negative values deeper.")}};

  for (int row = 0; row < PHOTO_ADJUSTMENT_COUNT; ++row) {
    PhotoAdjustmentRow& r = m_photoAdjustmentRows[row];
    QWidget* parent = photoAdjustmentsPanel;
    r.label = new QLabel(texts[row].label, parent);
    r.slider = new QSlider(Qt::Horizontal, parent);
    if (row == EXPOSURE) {
      // The slider counts hundredths of a stop.
      auto* field = new QDoubleSpinBox(parent);
      field->setRange(PhotoAdjustments::MIN_EXPOSURE, PhotoAdjustments::MAX_EXPOSURE);
      field->setDecimals(2);
      field->setSingleStep(0.05);
      field->setSuffix(QStringLiteral(" EV"));
      r.field = field;
      r.slider->setRange(qRound(PhotoAdjustments::MIN_EXPOSURE * 100), qRound(PhotoAdjustments::MAX_EXPOSURE * 100));
      r.slider->setSingleStep(5);
      r.slider->setPageStep(25);
    } else {
      auto* field = new QSpinBox(parent);
      field->setRange(PhotoAdjustments::MIN_VALUE, PhotoAdjustments::MAX_VALUE);
      r.field = field;
      r.slider->setRange(PhotoAdjustments::MIN_VALUE, PhotoAdjustments::MAX_VALUE);
      r.slider->setSingleStep(1);
      r.slider->setPageStep(10);
    }
    r.field->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    r.field->setKeyboardTracking(false);
    for (QWidget* widget :
         {static_cast<QWidget*>(r.label), static_cast<QWidget*>(r.field), static_cast<QWidget*>(r.slider)}) {
      widget->setToolTip(texts[row].toolTip);
    }

    QGridLayout* grid = (row <= TINT) ? colorBalanceGrid : toneGrid;
    const int gridRow = 2 * ((row <= TINT) ? row : row - EXPOSURE);
    grid->addWidget(r.label, gridRow, 0);
    grid->addWidget(r.field, gridRow, 1, Qt::AlignLeft);
    grid->addWidget(r.slider, gridRow + 1, 0, 1, 2);
  }
  // Equal fields, as wide as "-2.00 EV".
  QAbstractSpinBox* exposureField = m_photoAdjustmentRows[EXPOSURE].field;
  core::Utils::setNumberFieldWidth(exposureField, PhotoAdjustments::MIN_EXPOSURE);
  for (PhotoAdjustmentRow& r : m_photoAdjustmentRows) {
    r.field->setFixedWidth(exposureField->minimumWidth());
  }
  // The fields of both parts start at the same place.
  core::Utils::alignLabelColumns({colorBalanceGrid, toneGrid});
  for (QGridLayout* grid : {colorBalanceGrid, toneGrid}) {
    grid->setColumnStretch(1, 1);
  }
}

int OptionsWidget::photoAdjustmentSliderValue(const int row) const {
  const PhotoAdjustments& adjustments = m_colorParams.photoAdjustments();
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

void OptionsWidget::photoAdjustmentSliderChanged(const int row, const int value) {
  PhotoAdjustmentRow& r = m_photoAdjustmentRows[row];
  {
    const QSignalBlocker blocker(r.field);
    if (auto* field = qobject_cast<QDoubleSpinBox*>(r.field)) {
      field->setValue(value / 100.0);
    } else if (auto* field = qobject_cast<QSpinBox*>(r.field)) {
      field->setValue(value);
    }
  }
  // While dragging, the page is recalculated only once the slider is released.
  if (!r.slider->isSliderDown()) {
    commitPhotoAdjustments();
  }
}

void OptionsWidget::photoAdjustmentFieldChanged(const int row) {
  PhotoAdjustmentRow& r = m_photoAdjustmentRows[row];
  {
    const QSignalBlocker blocker(r.slider);
    if (auto* field = qobject_cast<QDoubleSpinBox*>(r.field)) {
      r.slider->setValue(qRound(field->value() * 100));
    } else if (auto* field = qobject_cast<QSpinBox*>(r.field)) {
      r.slider->setValue(field->value());
    }
  }
  commitPhotoAdjustments();
}

void OptionsWidget::commitPhotoAdjustments() {
  const auto value = [this](const int row) { return m_photoAdjustmentRows[row].slider->value(); };
  PhotoAdjustments adjustments;
  adjustments.setTemperature(value(TEMPERATURE));
  adjustments.setTint(value(TINT));
  adjustments.setExposure(value(EXPOSURE) / 100.0);
  adjustments.setContrast(value(CONTRAST));
  adjustments.setHighlights(value(HIGHLIGHTS));
  adjustments.setShadows(value(SHADOWS));
  adjustments.setWhites(value(WHITES));
  adjustments.setBlacks(value(BLACKS));
  adjustments.setKeepWhite(keepWhiteCB->isChecked());
  if (adjustments == m_colorParams.photoAdjustments()) {
    return;
  }

  m_colorParams.setPhotoAdjustments(adjustments);
  m_settings->setColorParams(m_pageId, m_colorParams);
  // Don't recreate the page for every step of a field or of the keyboard.
  m_delayedReloadRequest.start(300);
}

void OptionsWidget::updatePhotoAdjustmentsDisplay() {
  for (int row = 0; row < PHOTO_ADJUSTMENT_COUNT; ++row) {
    PhotoAdjustmentRow& r = m_photoAdjustmentRows[row];
    const QSignalBlocker sliderBlocker(r.slider);
    const QSignalBlocker fieldBlocker(r.field);
    const int value = photoAdjustmentSliderValue(row);
    r.slider->setValue(value);
    if (auto* field = qobject_cast<QDoubleSpinBox*>(r.field)) {
      field->setValue(value / 100.0);
    } else if (auto* field = qobject_cast<QSpinBox*>(r.field)) {
      field->setValue(value);
    }
  }
  const QSignalBlocker keepWhiteBlocker(keepWhiteCB);
  keepWhiteCB->setChecked(m_colorParams.photoAdjustments().keepWhite());

  // Not used in black and white; the color balance not with grayscale output, "Keep white" only
  // in color mode (in mixed mode only the pictures are changed).
  const ColorMode colorMode = m_colorParams.colorMode();
  const bool colorBalance = !m_colorParams.colorCommonOptions().isGrayscaleOutput();
  photoAdjustmentsPanel->setEnabled(colorMode != BLACK_AND_WHITE);
  colorBalanceHeading->setEnabled(colorBalance);
  for (const int row : {TEMPERATURE, TINT}) {
    m_photoAdjustmentRows[row].label->setEnabled(colorBalance);
    m_photoAdjustmentRows[row].field->setEnabled(colorBalance);
    m_photoAdjustmentRows[row].slider->setEnabled(colorBalance);
  }
  keepWhiteCB->setEnabled(colorMode == COLOR_GRAYSCALE);
}

void OptionsWidget::applyPhotoAdjustmentsButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Photo Adjustments"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applyPhotoAdjustmentsConfirmed);
  dialog->show();
}

void OptionsWidget::applyPhotoAdjustmentsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    ColorParams colorParams = m_settings->getParams(pageId).colorParams();
    colorParams.setPhotoAdjustments(m_colorParams.photoAdjustments());
    m_settings->setColorParams(pageId, colorParams);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::applySplittingButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Splitting"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applySplittingOptionsConfirmed);
  dialog->show();
}

void OptionsWidget::applySplittingOptionsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setSplittingOptions(pageId, m_splittingOptions);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::despeckleToggled(bool checked) {
  if (checked) {
    handleDespeckleLevelChange(0.1 * despeckleSlider->value());
  } else {
    handleDespeckleLevelChange(0);
  };

  despeckleSlider->setEnabled(checked);
  despeckleValueLabel->setEnabled(checked);
}

void OptionsWidget::despeckleSliderReleased() {
  const double value = 0.1 * despeckleSlider->value();
  handleDespeckleLevelChange(value);
}

void OptionsWidget::despeckleSliderValueChanged(int value) {
  const double newValue = 0.1 * value;

  const QString tooltipText(QLocale().toString(newValue, 'f', 1));
  despeckleSlider->setToolTip(tooltipText);
  despeckleValueLabel->setText(tooltipText);

  // Show the tooltip immediately.
  const QPoint center(despeckleSlider->rect().center());
  QPoint tooltipPos(despeckleSlider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(center.y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), despeckleSlider->width()));
  tooltipPos = despeckleSlider->mapToGlobal(tooltipPos);
  QToolTip::showText(tooltipPos, tooltipText, despeckleSlider);

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
      m_delayedReloadRequest.start(750);
    } else {
      emit reloadRequested();
    }
  }
}

void OptionsWidget::applyDespeckleButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Despeckling"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applyDespeckleConfirmed);
  dialog->show();
}

void OptionsWidget::applyDespeckleConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setDespeckleLevel(pageId, m_despeckleLevel);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::dewarpingModeChanged(const int mode) {
  DewarpingOptions opt(m_dewarpingOptions);
  opt.setDewarpingMode(static_cast<DewarpingMode>(mode));
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
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Dewarping"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applyDewarpingConfirmed);
  dialog->show();
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
    m_settings->setDewarpingOptions(pageId, opt);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    if (m_dewarpingOptions != opt) {
      m_dewarpingOptions = opt;


      // We also have to reload if we are currently on the "Fill Zones" tab,
      // as it makes use of original <-> dewarped coordinate mapping,
      // which is too hard to update without reloading.  For consistency,
      // we reload not just on TAB_FILL_ZONES but on all tabs except TAB_DEWARPING.
      // PS: the static original <-> dewarped mappings are constructed
      // in Task::UiUpdater::updateUI().  Look for "new DewarpingPointMapper" there.
      if ((opt.dewarpingMode() == AUTO) || (m_lastTab != TAB_DEWARPING) || (opt.dewarpingMode() == MARGINAL)) {
        // Switch to the Output tab after reloading.
        m_lastTab = TAB_OUTPUT;
        // These depend on the value of m_lastTab.
        updateDpiDisplay();
        updateColorsDisplay();
        updateDewarpingDisplay();

        emit reloadRequested();
      } else {
        // This one we have to call anyway, as it depends on m_dewarpingMode.
        updateDewarpingDisplay();
      }
    }
  }
}  // OptionsWidget::dewarpingChanged


void OptionsWidget::depthPerceptionChangedSlot(int val) {
  m_depthPerception.setValue(0.1 * val);
  const QString tooltipText(QLocale().toString(m_depthPerception.value(), 'f', 1));
  depthPerceptionSlider->setToolTip(tooltipText);
  depthPerceptionValueLabel->setText(tooltipText);

  // Show the tooltip immediately.
  const QPoint center(depthPerceptionSlider->rect().center());
  QPoint tooltipPos(depthPerceptionSlider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(center.y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), depthPerceptionSlider->width()));
  tooltipPos = depthPerceptionSlider->mapToGlobal(tooltipPos);
  QToolTip::showText(tooltipPos, tooltipText, depthPerceptionSlider);

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
  if (m_lastTab != TAB_DEWARPING) {
    m_delayedReloadRequest.start(750);
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
    savedDewarpingOptions = outputParams->outputImageParams().dewarpingMode();
    savedDistortionModel = outputParams->outputImageParams().distortionModel();
    savedDepthPerception = outputParams->outputImageParams().depthPerception();
    savedDespeckleLevel = outputParams->outputImageParams().despeckleLevel();
  }

  if (!PictureZoneComparator::equal(savedPictureZones, m_settings->pictureZonesForPage(m_pageId))) {
    emit reloadRequested();
    return;
  } else if (!FillZoneComparator::equal(savedFillZones, m_settings->fillZonesForPage(m_pageId))) {
    emit reloadRequested();
    return;
  }

  const Params params(m_settings->getParams(m_pageId));

  if (savedDespeckleLevel != params.despeckleLevel()) {
    emit reloadRequested();
    return;
  }

  if ((savedDewarpingOptions.dewarpingMode() == OFF) && (params.dewarpingOptions().dewarpingMode() == OFF)) {
  } else if (savedDepthPerception.value() != params.depthPerception().value()) {
    emit reloadRequested();
    return;
  } else if (savedDewarpingOptions.evenOutLetterWidths() != params.dewarpingOptions().evenOutLetterWidths()) {
    emit reloadRequested();
    return;
  } else if ((savedDewarpingOptions.dewarpingMode() == AUTO) && (params.dewarpingOptions().dewarpingMode() == AUTO)) {
  } else if ((savedDewarpingOptions.dewarpingMode() == MARGINAL)
             && (params.dewarpingOptions().dewarpingMode() == MARGINAL)) {
  } else if (!savedDistortionModel.matches(params.distortionModel())) {
    emit reloadRequested();
    return;
  } else if ((savedDewarpingOptions.dewarpingMode() == OFF) != (params.dewarpingOptions().dewarpingMode() == OFF)) {
    emit reloadRequested();
    return;
  }
}  // OptionsWidget::reloadIfNecessary

void OptionsWidget::updateDpiDisplay() {
  // Changing the resolution sets the same value for both directions.
  const QSignalBlocker blocker(dpiSelector);
  dpiSelector->setEditText(Utils::dpiText(std::max(m_outputDpi.horizontal(), m_outputDpi.vertical())));
}

void OptionsWidget::updateColorsDisplay() {
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
  colorDetectionMinColorSB->setValue(ApplicationSettings::getInstance().getColorDetectionMinColor());
  colorDetectionZonesCB->setChecked(ApplicationSettings::getInstance().isColorDetectionZones());
  colorDetectionMinPictureSB->setValue(ApplicationSettings::getInstance().getColorDetectionMinPicture());
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
  ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());

  if (!blackWhiteOptions.normalizeIllumination() && colorMode == MIXED) {
    colorCommonOptions.setNormalizeIllumination(false);
  }
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  fillMarginsCB->setChecked(colorCommonOptions.fillMargins());
  fillOffcutCB->setChecked(colorCommonOptions.fillOffcut());
  fillOutsidePageBoxCB->setChecked(colorCommonOptions.fillOutsidePageBox());
  // "Equalize illumination" shows the setting the color mode uses; "Also in picture zones" is the
  // color setting, used in mixed mode on top of the black and white one.
  equalizeIlluminationCB->setChecked((colorMode == COLOR_GRAYSCALE) ? colorCommonOptions.normalizeIllumination()
                                                                    : blackWhiteOptions.normalizeIllumination());
  equalizeIlluminationColorCB->setChecked(colorCommonOptions.normalizeIllumination());
  equalizeIlluminationColorCB->setEnabled(colorMode == MIXED && blackWhiteOptions.normalizeIllumination());
  grayscaleOutputCB->setChecked(colorCommonOptions.isGrayscaleOutput());
  grayscaleOutputCB->setEnabled(colorMode != BLACK_AND_WHITE);
  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  savitzkyGolaySmoothingCB->setEnabled(thresholdOptionsVisible);
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());
  morphologicalSmoothingCB->setEnabled(thresholdOptionsVisible);
  coloredDimmingSB->setValue(blackWhiteOptions.getColoredDimming());
  lowerBoundSB->setValue(blackWhiteOptions.getWolfLowerBound());
  upperBoundSB->setValue(blackWhiteOptions.getWolfUpperBound());

  pictureShapeOptions->setEnabled(pictureShapeVisible);
  thresholdOptions->setEnabled(thresholdOptionsVisible);
  despecklePanel->setEnabled(thresholdOptionsVisible);

  updatePhotoAdjustmentsDisplay();

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
  originalBackgroundCB->setEnabled(m_splittingOptions.isSplitOutput()
                                   && (m_splittingOptions.getSplittingMode() == BLACK_AND_WHITE_FOREGROUND));

  thresholdMethodBox->setCurrentIndex((int) blackWhiteOptions.getBinarizationMethod());
  binarizationOptions->setCurrentIndex((int) blackWhiteOptions.getBinarizationMethod());

  // The areas to fill are used in every color mode, the color not in black and white.
  fillingColorLabel->setEnabled(colorMode != BLACK_AND_WHITE);
  fillingColorBox->setEnabled(colorMode != BLACK_AND_WHITE);
  fillingColorBox->setCurrentIndex((int) colorCommonOptions.getFillingColor());

  colorSegmentationCB->setEnabled(thresholdOptionsVisible);
  updateColorReductionControls();
  // The check box first: the strength shown while it is off is only kept for turning it on.
  chromaCB->setChecked(colorCommonOptions.chromaDenoise());
  chromaOptionsWidget->setEnabled(colorCommonOptions.chromaDenoise());
  chromaRadius->setValue(colorCommonOptions.chromaRadius());
  chromaGrayLimit->setValue(colorCommonOptions.chromaGrayLimit());
  const bool subtractBgOn = colorCommonOptions.subtractBackgroundCoef() > 0.0;
  if (subtractBgOn) {
    m_subtractBgCoefWhenOn = colorCommonOptions.subtractBackgroundCoef();
  }
  subtractBgCB->setChecked(subtractBgOn);
  subtractBgOptionsWidget->setEnabled(subtractBgOn);
  subtractBgCoef->setValue(m_subtractBgCoefWhenOn);
  subtractBgRadius->setValue(colorCommonOptions.subtractBackgroundRadius());
  subtractBgWhiteCB->setChecked(colorCommonOptions.subtractBackgroundWhite());
  const bool knnOn = colorCommonOptions.knnCoef() > 0.0;
  if (knnOn) {
    m_knnCoefWhenOn = colorCommonOptions.knnCoef();
  }
  knnCB->setChecked(knnOn);
  knnOptionsWidget->setEnabled(knnOn);
  knnCoef->setValue(m_knnCoefWhenOn);
  knnRadius->setValue(colorCommonOptions.knnRadius());
  const bool sharpenOn = colorCommonOptions.sharpenCoef() > 0.0;
  if (sharpenOn) {
    m_sharpenCoefWhenOn = colorCommonOptions.sharpenCoef();
  }
  sharpenCB->setChecked(sharpenOn);
  sharpenOptionsWidget->setEnabled(sharpenOn);
  sharpenCoef->setValue(m_sharpenCoefWhenOn);
  sharpenRadius->setValue(colorCommonOptions.sharpenRadius());
  const bool wienerOn = colorCommonOptions.wienerCoef() > 0.0;
  if (wienerOn) {
    m_wienerCoefWhenOn = colorCommonOptions.wienerCoef();
  }
  wienerCB->setChecked(wienerOn);
  wienerOptionsWidget->setEnabled(wienerOn);
  wienerCoef->setValue(m_wienerCoefWhenOn);
  wienerWindowSize->setValue(colorCommonOptions.wienerWindowSize());
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

  for (int i = 0; i < binarizationOptions->count(); i++) {
    auto* widget = dynamic_cast<OptionsWidgetBinarization*>(binarizationOptions->widget(i));
    widget->updateUi(m_pageId);
  }

}  // OptionsWidget::updateColorsDisplay

void OptionsWidget::updateDewarpingDisplay() {
  // Shown in every view, grayed out while there is no dewarping.
  depthPerceptionPanel->setEnabled(m_dewarpingOptions.dewarpingMode() != OFF);
  evenOutLetterWidthsCB->setEnabled(m_dewarpingOptions.dewarpingMode() != OFF);

  {
    const QSignalBlocker modeBlocker(m_dewarpingModeGroup);
    const QSignalBlocker postDeskewBlocker(dewarpingPostDeskewCB);
    const QSignalBlocker evenOutBlocker(evenOutLetterWidthsCB);
    if (QAbstractButton* button = m_dewarpingModeGroup->button(m_dewarpingOptions.dewarpingMode())) {
      button->setChecked(true);
    }
    dewarpingPostDeskewCB->setChecked(m_dewarpingOptions.needPostDeskew());
    evenOutLetterWidthsCB->setChecked(m_dewarpingOptions.evenOutLetterWidths());
  }

  // The angle found by the post deskew is shown where it is used.
  QString postDeskewText = tr("Post deskew");
  if (m_dewarpingOptions.needPostDeskew()
      && ((m_dewarpingOptions.dewarpingMode() == MANUAL) || (m_dewarpingOptions.dewarpingMode() == MARGINAL))) {
    const double deskewAngle = -std::round(m_dewarpingOptions.getPostDeskewAngle() * 100) / 100;
    postDeskewText += " (" + QString::number(deskewAngle) + QChar(0x00B0) + ")";
  }
  dewarpingPostDeskewCB->setText(postDeskewText);
  // Only Manual and Marginal deskew the page after dewarping.
  dewarpingPostDeskewCB->setEnabled((m_dewarpingOptions.dewarpingMode() == MANUAL)
                                    || (m_dewarpingOptions.dewarpingMode() == MARGINAL));

  depthPerceptionSlider->blockSignals(true);
  depthPerceptionSlider->setValue(qRound(m_depthPerception.value() * 10));
  depthPerceptionSlider->blockSignals(false);
  depthPerceptionValueLabel->setText(QLocale().toString(m_depthPerception.value(), 'f', 1));
}

void OptionsWidget::savitzkyGolaySmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setSavitzkyGolaySmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::morphologicalSmoothingToggled(bool checked) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setMorphologicalSmoothingEnabled(checked);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::coloredDimmingChanged(const double value) {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setColoredDimming(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(750);
}

void OptionsWidget::binarizationBoundChanged() {
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setWolfLowerBound(lowerBoundSB->value());
  opt.setWolfUpperBound(upperBoundSB->value());
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(750);
}

void OptionsWidget::bwForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }

  originalBackgroundCB->setEnabled(checked);

  m_splittingOptions.setSplittingMode(BLACK_AND_WHITE_FOREGROUND);
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::colorForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }

  originalBackgroundCB->setEnabled(!checked);

  m_splittingOptions.setSplittingMode(COLOR_FOREGROUND);
  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::splittingToggled(bool checked) {
  m_splittingOptions.setSplitOutput(checked);

  bwForegroundRB->setEnabled(checked);
  colorForegroundRB->setEnabled(checked);
  originalBackgroundCB->setEnabled(checked && bwForegroundRB->isChecked());

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::originalBackgroundToggled(bool checked) {
  m_splittingOptions.setOriginalBackgroundEnabled(checked);

  m_settings->setSplittingOptions(m_pageId, m_splittingOptions);
  emit reloadRequested();
}

void OptionsWidget::chromaToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaDenoise(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  chromaOptionsWidget->setEnabled(checked);
  emit reloadRequested();
}

void OptionsWidget::chromaRadiusChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaRadius(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::chromaGrayLimitChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setChromaGrayLimit(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::subtractBgToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSubtractBackgroundCoef(checked ? m_subtractBgCoefWhenOn : 0.0);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  subtractBgOptionsWidget->setEnabled(checked);
  emit reloadRequested();
}

void OptionsWidget::subtractBgCoefChanged(double value) {
  if (!subtractBgCB->isChecked()) {
    return;
  }
  m_subtractBgCoefWhenOn = value;
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSubtractBackgroundCoef(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::subtractBgRadiusChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSubtractBackgroundRadius(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::subtractBgWhiteToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSubtractBackgroundWhite(checked);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
}

void OptionsWidget::knnToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setKnnCoef(checked ? m_knnCoefWhenOn : 0.0);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  knnOptionsWidget->setEnabled(checked);
  emit reloadRequested();
}

void OptionsWidget::knnCoefChanged(double value) {
  if (!knnCB->isChecked()) {
    return;
  }
  m_knnCoefWhenOn = value;
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setKnnCoef(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::knnRadiusChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setKnnRadius(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::sharpenToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSharpenCoef(checked ? m_sharpenCoefWhenOn : 0.0);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  sharpenOptionsWidget->setEnabled(checked);
  emit reloadRequested();
}

void OptionsWidget::sharpenCoefChanged(double value) {
  if (!sharpenCB->isChecked()) {
    return;
  }
  m_sharpenCoefWhenOn = value;
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSharpenCoef(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::sharpenRadiusChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setSharpenRadius(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::wienerToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setWienerCoef(checked ? m_wienerCoefWhenOn : 0.0);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  wienerOptionsWidget->setEnabled(checked);
  emit reloadRequested();
}

void OptionsWidget::wienerCoefChanged(double value) {
  if (!wienerCB->isChecked()) {
    return;
  }
  m_wienerCoefWhenOn = value;
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setWienerCoef(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::wienerWindowSizeChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  colorCommonOptions.setWienerWindowSize(value);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::colorSegmentationToggled(bool checked) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setEnabled(checked);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  updateColorReductionControls();

  emit reloadRequested();
}

void OptionsWidget::reduceNoiseChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setNoiseReduction(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::redAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setRedThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::greenAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setGreenThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::blueAdjustmentChanged(int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setBlueThresholdAdjustment(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

int OptionsWidget::colorReduction() const {
  // The items of "Reduce colors": none, posterize, K-means.
  if (m_colorParams.blackWhiteOptions().getColorSegmenterOptions().isKMeansEnabled()) {
    return 2;
  }
  return m_colorParams.colorCommonOptions().getPosterizationOptions().isEnabled() ? 1 : 0;
}

void OptionsWidget::updateColorReductionControls() {
  const ColorMode colorMode = m_colorParams.colorMode();
  const bool textColors = (colorMode == BLACK_AND_WHITE) || (colorMode == MIXED);
  const bool segmentation = textColors && m_colorParams.blackWhiteOptions().getColorSegmenterOptions().isEnabled();
  const int reduction = colorReduction();
  segmenterOptionsWidget->setEnabled(segmentation);
  for (QWidget* widget : std::initializer_list<QWidget*>{kmeansColorsLabel, kmeansColorsSB, kmeansFixBlackCB}) {
    widget->setEnabled(segmentation && (reduction == 2));
  }
  const bool posterize = m_colorParams.colorCommonOptions().getPosterizationOptions().isEnabled();
  if (textColors) {
    // The colors of the text are reduced as chosen in "Reduce colors"; the check box only shows it.
    posterizeCB->setEnabled(false);
    posterizeCB->setChecked(reduction == 1);
    posterizeOptionsWidget->setEnabled(segmentation && (reduction == 1));
  } else {
    posterizeCB->setEnabled(true);
    posterizeCB->setChecked(posterize);
    posterizeOptionsWidget->setEnabled(posterize);
  }
}

void OptionsWidget::colorReductionChanged(const int index) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setKMeansEnabled(index == 2);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setEnabled(index == 1);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  updateColorReductionControls();
  emit reloadRequested();
}

void OptionsWidget::kmeansColorsChanged(const int value) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setKMeansColors(value);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  m_delayedReloadRequest.start(750);
}

void OptionsWidget::kmeansFixBlackToggled(const bool checked) {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setKMeansBlackFixed(checked);
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit reloadRequested();
}

void OptionsWidget::posterizeToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setEnabled(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  posterizeOptionsWidget->setEnabled(checked);

  emit reloadRequested();
}

void OptionsWidget::posterizeLevelChanged(int value) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setLevel(value);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  m_delayedReloadRequest.start(750);
}

void OptionsWidget::posterizeNormalizationToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setNormalizationEnabled(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
}

void OptionsWidget::posterizeForceBwToggled(bool checked) {
  ColorCommonOptions colorCommonOptions = m_colorParams.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setForceBlackAndWhite(checked);
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  m_colorParams.setColorCommonOptions(colorCommonOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

  emit reloadRequested();
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
  widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  widget->adjustSize();
  binarizationOptions->adjustSize();

  connect(static_cast<OptionsWidgetBinarization*>(widget), &OptionsWidgetBinarization::stateChanged, this,
          &OptionsWidget::binarizationSettingsChanged);
}

void OptionsWidget::addOptionsWidgetBinarization(OptionsWidgetBinarization* widget) {
  widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
  binarizationOptions->addWidget(widget);
}

void OptionsWidget::sendReloadRequested() {
  emit reloadRequested();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

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
  CONNECT(subtractBgCB, &QAbstractButton::clicked, this, &OptionsWidget::subtractBgToggled);
  CONNECT(subtractBgCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::subtractBgCoefChanged);
  CONNECT(subtractBgRadius, &QSpinBox::valueChanged, this, &OptionsWidget::subtractBgRadiusChanged);
  CONNECT(subtractBgWhiteCB, &QAbstractButton::clicked, this, &OptionsWidget::subtractBgWhiteToggled);
  CONNECT(knnCB, &QAbstractButton::clicked, this, &OptionsWidget::knnToggled);
  CONNECT(knnCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::knnCoefChanged);
  CONNECT(knnRadius, &QSpinBox::valueChanged, this, &OptionsWidget::knnRadiusChanged);
  CONNECT(sharpenCB, &QAbstractButton::clicked, this, &OptionsWidget::sharpenToggled);
  CONNECT(sharpenCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::sharpenCoefChanged);
  CONNECT(sharpenRadius, &QSpinBox::valueChanged, this, &OptionsWidget::sharpenRadiusChanged);
  CONNECT(wienerCB, &QAbstractButton::clicked, this, &OptionsWidget::wienerToggled);
  CONNECT(wienerCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::wienerCoefChanged);
  CONNECT(wienerWindowSize, &QSpinBox::valueChanged, this, &OptionsWidget::wienerWindowSizeChanged);
  CONNECT(colorSegmentationCB, &QAbstractButton::clicked, this, &OptionsWidget::colorSegmentationToggled);
  CONNECT(reduceNoiseSB, &QSpinBox::valueChanged, this, &OptionsWidget::reduceNoiseChanged);
  CONNECT(colorReductionCB, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &OptionsWidget::colorReductionChanged);
  CONNECT(kmeansColorsSB, &QSpinBox::valueChanged, this, &OptionsWidget::kmeansColorsChanged);
  CONNECT(kmeansFixBlackCB, &QAbstractButton::clicked, this, &OptionsWidget::kmeansFixBlackToggled);
  CONNECT(redAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::redAdjustmentChanged);
  CONNECT(greenAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::greenAdjustmentChanged);
  CONNECT(blueAdjustmentSB, &QSpinBox::valueChanged, this, &OptionsWidget::blueAdjustmentChanged);
  CONNECT(posterizeCB, &QAbstractButton::clicked, this, &OptionsWidget::posterizeToggled);
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

  CONNECT(m_dewarpingModeGroup, &QButtonGroup::idClicked, this, &OptionsWidget::dewarpingModeChanged);
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

  for (int row = 0; row < PHOTO_ADJUSTMENT_COUNT; ++row) {
    const PhotoAdjustmentRow& r = m_photoAdjustmentRows[row];
    CONNECT(r.slider, &QAbstractSlider::valueChanged, this,
            [this, row](const int value) { photoAdjustmentSliderChanged(row, value); });
    CONNECT(r.slider, &QAbstractSlider::sliderReleased, this, &OptionsWidget::commitPhotoAdjustments);
    if (auto* field = qobject_cast<QDoubleSpinBox*>(r.field)) {
      CONNECT(field, &QDoubleSpinBox::valueChanged, this, [this, row] { photoAdjustmentFieldChanged(row); });
    } else if (auto* field = qobject_cast<QSpinBox*>(r.field)) {
      CONNECT(field, &QSpinBox::valueChanged, this, [this, row] { photoAdjustmentFieldChanged(row); });
    }
  }
  CONNECT(keepWhiteCB, &QAbstractButton::clicked, this, &OptionsWidget::commitPhotoAdjustments);
  CONNECT(applyPhotoAdjustmentsButton, &QAbstractButton::clicked, this,
          &OptionsWidget::applyPhotoAdjustmentsButtonClicked);

  CONNECT(blackOnWhiteCB, &QAbstractButton::clicked, this, &OptionsWidget::blackOnWhiteToggled);
  CONNECT(applyProcessingOptionsButton, &QAbstractButton::clicked, this, &OptionsWidget::applyProcessingParamsClicked);
}

#undef CONNECT

ImageViewTab OptionsWidget::lastTab() const {
  return m_lastTab;
}

const DepthPerception& OptionsWidget::depthPerception() const {
  return m_depthPerception;
}

void OptionsWidget::blackOnWhiteToggled(bool value) {
  m_settings->setBlackOnWhite(m_pageId, value);
  OutputProcessingParams processingParams = m_settings->getOutputProcessingParams(m_pageId);
  processingParams.setBlackOnWhiteSetManually(true);
  m_settings->setOutputProcessingParams(m_pageId, processingParams);

  emit reloadRequested();
}

void OptionsWidget::applyProcessingParamsClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Content"));
  connect(dialog, &ApplyColorsDialog::accepted, this, &OptionsWidget::applyProcessingParamsConfirmed);
  dialog->show();
}

void OptionsWidget::applyProcessingParamsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setBlackOnWhite(pageId, m_settings->getParams(m_pageId).isBlackOnWhite());
    OutputProcessingParams processingParams = m_settings->getOutputProcessingParams(pageId);
    processingParams.setBlackOnWhiteSetManually(true);
    m_settings->setOutputProcessingParams(pageId, processingParams);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  if (pages.find(m_pageId) != pages.end()) {
    emit reloadRequested();
  }
}

void OptionsWidget::updateProcessingDisplay() {
  blackOnWhiteCB->setChecked(m_settings->getParams(m_pageId).isBlackOnWhite());
}
}  // namespace output
