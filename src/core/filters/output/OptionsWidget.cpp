// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <core/ApplicationSettings.h>
#include <tiff.h>

#include <QButtonGroup>
#include <QCoreApplication>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QToolTip>
#include <algorithm>
#include <utility>

#include "../../SectionHeading.h"
#include "../../Utils.h"
#include "ApplyColorsDialog.h"
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

  m_delayedReloadRequest.setSingleShot(true);

  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(0.5 * 10));
  despeckleSlider->setMaximum(qRound(3.5 * 10));

  colorModeSelector->addItem(tr("Black and White"), BLACK_AND_WHITE);
  colorModeSelector->addItem(tr("Color / Grayscale"), COLOR_GRAYSCALE);
  colorModeSelector->addItem(tr("Mixed"), MIXED);

  thresholdMethodBox->addItem(tr("Otsu"), T_OTSU);
  thresholdMethodBox->addItem(tr("Sauvola"), T_SAUVOLA);
  thresholdMethodBox->addItem(tr("Wolf"), T_WOLF);
  thresholdMethodBox->addItem(tr("Fox"), T_FOX);
  thresholdMethodBox->addItem(tr("Window"), T_WINDOW);
  thresholdMethodBox->addItem(tr("Bradley"), T_BRADLEY);
  thresholdMethodBox->addItem(tr("Grad"), T_GRAD);
  thresholdMethodBox->addItem(tr("EdgePlus"), T_EDGEPLUS);
  thresholdMethodBox->addItem(tr("BlurDiv"), T_BLURDIV);
  thresholdMethodBox->addItem(tr("EdgeDiv"), T_EDGEDIV);

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
  updateBinarizationOptionsDisplay(binarizationOptions->currentIndex());

  pictureShapeSelector->addItem(tr("Off"), OFF_SHAPE);
  pictureShapeSelector->addItem(tr("Free shape"), FREE_SHAPE);
  pictureShapeSelector->addItem(tr("Rectangle"), RECTANGULAR_SHAPE);

  // Common resolutions; others can be typed in.
  for (const int dpi : {300, 400, 600, 1200}) {
    dpiSelector->addItem(Utils::dpiText(dpi));
  }
  dpiSelector->setValidator(Utils::createDpiValidator(dpiSelector));

  m_dewarpingModeGroup = new QButtonGroup(this);
  m_dewarpingModeGroup->addButton(dewarpingOffBtn, OFF);
  m_dewarpingModeGroup->addButton(dewarpingAutoBtn, AUTO);
  m_dewarpingModeGroup->addButton(dewarpingMarginalBtn, MARGINAL);
  m_dewarpingModeGroup->addButton(dewarpingManualBtn, MANUAL);

  updateDpiDisplay();
  updateColorsDisplay();
  updateDewarpingDisplay();

  connect(binarizationOptions, SIGNAL(currentChanged(int)), this, SLOT(updateBinarizationOptionsDisplay(int)));

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
  auto* colorMethodLabel = new QLabel(tr("Method:"));
  // The label column is as wide as in the PDF compression panel, whose longest label is this one,
  // so the lists stand at the same place in both steps.
  colorMethodLabel->setMinimumWidth(colorMethodLabel->fontMetrics().horizontalAdvance(
      QCoreApplication::translate("PdfExportView", "Resolution of split pages:")));
  layout->addRow(colorMethodLabel, m_tiffColorCompression);

  m_tiffJpegQuality = new QSpinBox;
  m_tiffJpegQuality->setRange(10, 100);
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

void OptionsWidget::colorModeChanged(const int idx) {
  const int mode = colorModeSelector->itemData(idx).toInt();
  m_colorParams.setColorMode((ColorMode) mode);
  m_settings->setColorParams(m_pageId, m_colorParams);
  updateColorsDisplay();
  emit reloadRequested();
}

void OptionsWidget::thresholdMethodChanged(int idx) {
  const BinarizationMethod method = (BinarizationMethod) thresholdMethodBox->itemData(idx).toInt();
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  blackWhiteOptions.setBinarizationMethod(method);
  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
  m_settings->setColorParams(m_pageId, m_colorParams);

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
  BlackWhiteOptions blackWhiteOptions(m_colorParams.blackWhiteOptions());
  blackWhiteOptions.setNormalizeIllumination(checked);

  if (m_colorParams.colorMode() == MIXED) {
    if (!checked) {
      ColorCommonOptions colorCommonOptions(m_colorParams.colorCommonOptions());
      colorCommonOptions.setNormalizeIllumination(false);
      equalizeIlluminationColorCB->setChecked(false);
      m_colorParams.setColorCommonOptions(colorCommonOptions);
    }
    equalizeIlluminationColorCB->setEnabled(checked);
  }

  m_colorParams.setBlackWhiteOptions(blackWhiteOptions);
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
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this, SLOT(applyDpiConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::applyDpiConfirmed(const std::set<PageId>& pages) {
  dpiChanged(pages, m_outputDpi);
}

void OptionsWidget::applyColorsButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this, SLOT(applyColorsConfirmed(const std::set<PageId>&)));
  dialog->show();
}

void OptionsWidget::dpiChanged(const std::set<PageId>& pages, const Dpi& dpi) {
  for (const PageId& pageId : pages) {
    Params params(m_settings->getParams(pageId));
    if (params.dewarpingOptions().dewarpingMode() == AUTO) {
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

void OptionsWidget::applyColorsConfirmed(const std::set<PageId>& pages) {
  for (const PageId& pageId : pages) {
    m_settings->setColorParams(pageId, m_colorParams);
    m_settings->setPictureShapeOptions(pageId, m_pictureShapeOptions);
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
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applySplittingOptionsConfirmed(const std::set<PageId>&)));
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
}

void OptionsWidget::despeckleSliderReleased() {
  const double value = 0.1 * despeckleSlider->value();
  handleDespeckleLevelChange(value);
}

void OptionsWidget::despeckleSliderValueChanged(int value) {
  const double newValue = 0.1 * value;

  const QString tooltipText(QString::number(newValue));
  despeckleSlider->setToolTip(tooltipText);

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
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applyDespeckleConfirmed(const std::set<PageId>&)));
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

void OptionsWidget::applyDewarpingButtonClicked() {
  auto* dialog = new ApplyColorsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Dewarping"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applyDewarpingConfirmed(const std::set<PageId>&)));
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
  const QString tooltipText(QString::number(m_depthPerception.value()));
  depthPerceptionSlider->setToolTip(tooltipText);

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
  colorModeSelector->blockSignals(true);

  const ColorMode colorMode = m_colorParams.colorMode();
  const int colorModeIdx = colorModeSelector->findData(colorMode);
  colorModeSelector->setCurrentIndex(colorModeIdx);

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
  equalizeIlluminationCB->setChecked(blackWhiteOptions.normalizeIllumination());
  equalizeIlluminationCB->setEnabled(colorMode != COLOR_GRAYSCALE);
  equalizeIlluminationColorCB->setChecked(colorCommonOptions.normalizeIllumination());
  equalizeIlluminationColorCB->setEnabled(colorMode == COLOR_GRAYSCALE
                                          || (colorMode == MIXED && blackWhiteOptions.normalizeIllumination()));
  grayscaleOutputCB->setChecked(colorCommonOptions.isGrayscaleOutput());
  grayscaleOutputCB->setEnabled(colorMode != BLACK_AND_WHITE);
  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  savitzkyGolaySmoothingCB->setEnabled(thresholdOptionsVisible);
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());
  morphologicalSmoothingCB->setEnabled(thresholdOptionsVisible);

  pictureShapeOptions->setEnabled(pictureShapeVisible);
  thresholdOptions->setEnabled(thresholdOptionsVisible);
  despecklePanel->setEnabled(thresholdOptionsVisible);

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
  segmenterOptionsWidget->setEnabled(thresholdOptionsVisible
                                     && blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  if (thresholdOptionsVisible) {
    posterizeCB->setEnabled(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
    posterizeOptionsWidget->setEnabled(blackWhiteOptions.getColorSegmenterOptions().isEnabled()
                                       && colorCommonOptions.getPosterizationOptions().isEnabled());
  } else {
    posterizeCB->setEnabled(true);
    posterizeOptionsWidget->setEnabled(colorCommonOptions.getPosterizationOptions().isEnabled());
  }
  // The check box first: the strength shown while it is off is only kept for turning it on.
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
  posterizeCB->setChecked(colorCommonOptions.getPosterizationOptions().isEnabled());
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
    despeckleSlider->setToolTip(QString::number(0.1 * despeckleSlider->value()));
  }

  for (int i = 0; i < binarizationOptions->count(); i++) {
    auto* widget = dynamic_cast<OptionsWidgetBinarization*>(binarizationOptions->widget(i));
    widget->updateUi(m_pageId);
  }

  colorModeSelector->blockSignals(false);
}  // OptionsWidget::updateColorsDisplay

void OptionsWidget::updateDewarpingDisplay() {
  // Shown in every view, grayed out while there is no dewarping.
  depthPerceptionPanel->setEnabled(m_dewarpingOptions.dewarpingMode() != OFF);

  {
    const QSignalBlocker modeBlocker(m_dewarpingModeGroup);
    const QSignalBlocker postDeskewBlocker(dewarpingPostDeskewCB);
    if (QAbstractButton* button = m_dewarpingModeGroup->button(m_dewarpingOptions.dewarpingMode())) {
      button->setChecked(true);
    }
    dewarpingPostDeskewCB->setChecked(m_dewarpingOptions.needPostDeskew());
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

  segmenterOptionsWidget->setEnabled(checked);
  if ((m_colorParams.colorMode() == BLACK_AND_WHITE) || (m_colorParams.colorMode() == MIXED)) {
    posterizeCB->setEnabled(checked);
    posterizeOptionsWidget->setEnabled(checked && posterizeCB->isChecked());
  }

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

    disconnect(currentWidget, SIGNAL(stateChanged()), this, SLOT(binarizationSettingsChanged()));
  }

  QWidget* widget = binarizationOptions->widget(idx);
  widget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
  widget->adjustSize();
  binarizationOptions->adjustSize();

  connect(widget, SIGNAL(stateChanged()), this, SLOT(binarizationSettingsChanged()));
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
  CONNECT(dpiSelector, SIGNAL(activated(int)), this, SLOT(dpiSelectionChanged()));
  CONNECT(dpiSelector->lineEdit(), SIGNAL(editingFinished()), this, SLOT(dpiSelectionChanged()));
  CONNECT(applyDpiButton, SIGNAL(clicked()), this, SLOT(applyDpiButtonClicked()));
  CONNECT(colorModeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(colorModeChanged(int)));
  CONNECT(thresholdMethodBox, SIGNAL(currentIndexChanged(int)), this, SLOT(thresholdMethodChanged(int)));
  CONNECT(fillingColorBox, SIGNAL(currentIndexChanged(int)), this, SLOT(fillingColorChanged(int)));
  CONNECT(pictureShapeSelector, SIGNAL(currentIndexChanged(int)), this, SLOT(pictureShapeChanged(int)));
  CONNECT(pictureShapeSensitivitySB, SIGNAL(valueChanged(int)), this, SLOT(pictureShapeSensitivityChanged(int)));
  CONNECT(higherSearchSensitivityCB, SIGNAL(clicked(bool)), this, SLOT(higherSearchSensivityToggled(bool)));

  CONNECT(wienerCB, SIGNAL(clicked(bool)), this, SLOT(wienerToggled(bool)));
  CONNECT(wienerCoef, SIGNAL(valueChanged(double)), this, SLOT(wienerCoefChanged(double)));
  CONNECT(wienerWindowSize, SIGNAL(valueChanged(int)), this, SLOT(wienerWindowSizeChanged(int)));
  CONNECT(colorSegmentationCB, SIGNAL(clicked(bool)), this, SLOT(colorSegmentationToggled(bool)));
  CONNECT(reduceNoiseSB, SIGNAL(valueChanged(int)), this, SLOT(reduceNoiseChanged(int)));
  CONNECT(redAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(redAdjustmentChanged(int)));
  CONNECT(greenAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(greenAdjustmentChanged(int)));
  CONNECT(blueAdjustmentSB, SIGNAL(valueChanged(int)), this, SLOT(blueAdjustmentChanged(int)));
  CONNECT(posterizeCB, SIGNAL(clicked(bool)), this, SLOT(posterizeToggled(bool)));
  CONNECT(posterizeLevelSB, SIGNAL(valueChanged(int)), this, SLOT(posterizeLevelChanged(int)));
  CONNECT(posterizeNormalizationCB, SIGNAL(clicked(bool)), this, SLOT(posterizeNormalizationToggled(bool)));
  CONNECT(posterizeForceBwCB, SIGNAL(clicked(bool)), this, SLOT(posterizeForceBwToggled(bool)));

  CONNECT(fillMarginsCB, SIGNAL(clicked(bool)), this, SLOT(fillMarginsToggled(bool)));
  CONNECT(fillOffcutCB, SIGNAL(clicked(bool)), this, SLOT(fillOffcutToggled(bool)));
  CONNECT(fillOutsidePageBoxCB, SIGNAL(clicked(bool)), this, SLOT(fillOutsidePageBoxToggled(bool)));
  CONNECT(equalizeIlluminationCB, SIGNAL(clicked(bool)), this, SLOT(equalizeIlluminationToggled(bool)));
  CONNECT(equalizeIlluminationColorCB, SIGNAL(clicked(bool)), this, SLOT(equalizeIlluminationColorToggled(bool)));
  CONNECT(grayscaleOutputCB, SIGNAL(clicked(bool)), this, SLOT(grayscaleOutputToggled(bool)));
  CONNECT(savitzkyGolaySmoothingCB, SIGNAL(clicked(bool)), this, SLOT(savitzkyGolaySmoothingToggled(bool)));
  CONNECT(morphologicalSmoothingCB, SIGNAL(clicked(bool)), this, SLOT(morphologicalSmoothingToggled(bool)));
  CONNECT(splittingCB, SIGNAL(clicked(bool)), this, SLOT(splittingToggled(bool)));
  CONNECT(bwForegroundRB, SIGNAL(clicked(bool)), this, SLOT(bwForegroundToggled(bool)));
  CONNECT(colorForegroundRB, SIGNAL(clicked(bool)), this, SLOT(colorForegroundToggled(bool)));
  CONNECT(originalBackgroundCB, SIGNAL(clicked(bool)), this, SLOT(originalBackgroundToggled(bool)));
  CONNECT(applyColorsButton, SIGNAL(clicked()), this, SLOT(applyColorsButtonClicked()));

  CONNECT(applySplittingButton, SIGNAL(clicked()), this, SLOT(applySplittingButtonClicked()));

  CONNECT(m_dewarpingModeGroup, SIGNAL(idClicked(int)), this, SLOT(dewarpingModeChanged(int)));
  CONNECT(dewarpingPostDeskewCB, SIGNAL(clicked(bool)), this, SLOT(dewarpingPostDeskewToggled(bool)));
  CONNECT(applyDewarpingButton, SIGNAL(clicked()), this, SLOT(applyDewarpingButtonClicked()));


  CONNECT(despeckleCB, SIGNAL(clicked(bool)), this, SLOT(despeckleToggled(bool)));
  CONNECT(despeckleSlider, SIGNAL(sliderReleased()), this, SLOT(despeckleSliderReleased()));
  CONNECT(despeckleSlider, SIGNAL(valueChanged(int)), this, SLOT(despeckleSliderValueChanged(int)));
  CONNECT(applyDespeckleButton, SIGNAL(clicked()), this, SLOT(applyDespeckleButtonClicked()));
  CONNECT(depthPerceptionSlider, SIGNAL(valueChanged(int)), this, SLOT(depthPerceptionChangedSlot(int)));
  CONNECT(&m_delayedReloadRequest, SIGNAL(timeout()), this, SLOT(sendReloadRequested()));

  CONNECT(blackOnWhiteCB, SIGNAL(clicked(bool)), this, SLOT(blackOnWhiteToggled(bool)));
  CONNECT(applyProcessingOptionsButton, SIGNAL(clicked()), this, SLOT(applyProcessingParamsClicked()));
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
  dialog->setWindowTitle(tr("Apply Processing"));
  connect(dialog, SIGNAL(accepted(const std::set<PageId>&)), this,
          SLOT(applyProcessingParamsConfirmed(const std::set<PageId>&)));
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
