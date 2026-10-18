// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DefaultParamsDialog.h"

#include <core/ApplicationSettings.h>
#include <core/IconProvider.h>
#include <filters/output/BinarizationMethods.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/page_split/LayoutType.h>

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QWindow>
#include <QtWidgets/QButtonGroup>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QToolTip>
#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

#include "CollapsibleGroupBox.h"
#include "DefaultParamsProvider.h"
#include "UnitsProvider.h"
#include "Utils.h"

using namespace core;
using namespace page_split;
using namespace output;
using namespace page_layout;

DefaultParamsDialog::DefaultParamsDialog(QWidget* parent)
    : QDialog(parent),
      m_leftRightLinkEnabled(true),
      m_topBottomLinkEnabled(true),
      m_currentUnits(MILLIMETRES),
      m_connectionManager(std::bind(&DefaultParamsDialog::setupUiConnections, this)) {
  setupUi(this);
  // Bold group titles, as in the options panels.
  for (QGroupBox* box : findChildren<QGroupBox*>()) {
    CollapsibleGroupBox::makeTitleBold(box);
  }
  setupIcons();

  // The deskew mode buttons and the oblique mode buttons share a parent widget, so autoExclusive
  // would treat all four as one group. Explicit groups keep the two pairs apart.
  auto* deskewModeGroup = new QButtonGroup(this);
  deskewModeGroup->addButton(deskewAutoBtn);
  deskewModeGroup->addButton(deskewManualBtn);
  auto* deskewObliqueModeGroup = new QButtonGroup(this);
  deskewObliqueModeGroup->addButton(deskewObliqueAutoBtn);
  deskewObliqueModeGroup->addButton(deskewObliqueManualBtn);

#ifndef ENABLE_OCR
  // The color mode detection comes with OCR (Leptonica).
  colorDetectionPanel->setVisible(false);
#endif

  fillingColorBox->addItem(tr("Background"), FILL_BACKGROUND);
  fillingColorBox->addItem(tr("White"), FILL_WHITE);
  fillingColorBox->addItem(tr("Black"), FILL_BLACK);

  fillBinarizationMethodBox(thresholdMethodBox);

  pictureShapeSelector->addItem(tr("Off"), OFF_SHAPE);
  pictureShapeSelector->addItem(tr("Contour mask"), FREE_SHAPE);
  pictureShapeSelector->addItem(tr("Rectangle"), RECTANGULAR_SHAPE);
  // The order as in the Output step: none, posterize, K-means.
  colorReductionCB->addItem(tr("None", "color reduction"));
  colorReductionCB->addItem(tr("Posterize"));
  colorReductionCB->addItem(tr("K-means"));

  // Common resolutions; others can be typed in, as in the Output step.
  for (const int dpi : {300, 400, 600, 1200}) {
    dpiSelector->addItem(Utils::dpiText(dpi));
  }
  dpiSelector->setValidator(Utils::createDpiValidator(dpiSelector));
  Utils::setDpiFieldWidth(dpiSelector);
  // "Colored" and "Bounds" apply to all methods and line up with the fields of the method shown.
  QList<QGridLayout*> binarizationGrids = binarizationOptions->findChildren<QGridLayout*>();
  binarizationGrids.append(allMethodsLayout);
  Utils::alignLabelColumns(binarizationGrids);
  // The fields of the enhancement filters start at the same place; column 0 is the indent.
  Utils::alignLabelColumns(
      {chromaOptionsLayout, subtractBgOptionsLayout, knnOptionsLayout, wienerOptionsLayout, sharpenOptionsLayout}, 1);
  // The settings of the threshold method are indented below the method list, as the settings of
  // the enhancement filters below their check boxes (a 15 px indent column plus the spacing).
  const int methodIndent = 15 + chromaOptionsLayout->horizontalSpacing();
  binarizationOptions->setContentsMargins(methodIndent, 0, 0, 0);
  allMethodsLayout->setContentsMargins(methodIndent, 0, 0, 0);
  // "Reduce colors" starts where "Reduce noise" starts, its settings one indent further. The list
  // has its own row, so that its width doesn't move the fields of the settings.
  colorReductionChoiceLayout->setSpacing(segmenterChannelsLayout->horizontalSpacing());
  colorReductionLayout->setHorizontalSpacing(segmenterChannelsLayout->horizontalSpacing());
  // Percent fields are as wide as "1000 %".
  for (QSpinBox* spinBox : {maxEnlargeSpinBox, minReduceSpinBox, fillFactorSpinBox, borderToleranceSpinBox,
                            pictureShapeSensitivitySB, colorDetectionMinColorSB, colorDetectionMinPictureSB}) {
    Utils::setNumberFieldWidth(spinBox, 1000);
  }
  // Pixel fields of the filters are as wide as "999 px", fields without a unit as "-100".
  for (QSpinBox* spinBox : {chromaRadius, subtractBgRadius, knnRadius, wienerWindowSize, sharpenRadius,
                            sauvolaWindowSize, wolfWindowSize}) {
    Utils::setNumberFieldWidth(spinBox, 999);
  }
  for (QAbstractSpinBox* spinBox : std::initializer_list<QAbstractSpinBox*>{
           chromaGrayLimit, subtractBgCoef, knnCoef, wienerCoef, sharpenCoef, otsuDelta, sauvolaDelta, sauvolaCoef,
           wolfDelta, lowerBound, upperBound, coloredDimmingSB, wolfCoef, redAdjustmentSB, greenAdjustmentSB,
           blueAdjustmentSB, reduceNoiseSB, kmeansColorsSB, posterizeLevelSB}) {
    Utils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_dewarpingModeGroup = new QButtonGroup(this);
  m_dewarpingModeGroup->addButton(dewarpingOffBtn, OFF);
  m_dewarpingModeGroup->addButton(dewarpingAutoBtn, AUTO);
  m_dewarpingModeGroup->addButton(dewarpingMarginalBtn, MARGINAL);
  m_dewarpingModeGroup->addButton(dewarpingManualBtn, MANUAL);

  m_reservedProfileNames.insert("Default");
  m_reservedProfileNames.insert("Minimal");
  m_reservedProfileNames.insert("Custom");

  profileCB->addItem(tr("Default"), "Default");
  profileCB->addItem(tr("Minimal"), "Minimal");
  const std::list<QString> profileList = m_profileManager.getProfileList();
  for (const QString& profileName : profileList) {
    if (!isProfileNameReserved(profileName)) {
      profileCB->addItem(profileName, profileName);
    }
  }
  m_customProfileItemIdx = profileCB->count();
  profileCB->addItem(tr("Custom"), "Custom");
  // The name typed for Custom is saved with the Save button or Enter, never added as an item by the box.
  profileCB->setInsertPolicy(QComboBox::NoInsert);

  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Default")));
  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Minimal")));
  m_reservedProfileNames.insert(profileCB->itemText(profileCB->findData("Custom")));

  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);

  Utils::mapSetValue(m_alignmentByButton, alignTopLeftBtn, Alignment(Alignment::TOP, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignTopBtn, Alignment(Alignment::TOP, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignTopRightBtn, Alignment(Alignment::TOP, Alignment::RIGHT));
  Utils::mapSetValue(m_alignmentByButton, alignLeftBtn, Alignment(Alignment::VCENTER, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignCenterBtn, Alignment(Alignment::VCENTER, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignRightBtn, Alignment(Alignment::VCENTER, Alignment::RIGHT));
  Utils::mapSetValue(m_alignmentByButton, alignBottomLeftBtn, Alignment(Alignment::BOTTOM, Alignment::LEFT));
  Utils::mapSetValue(m_alignmentByButton, alignBottomBtn, Alignment(Alignment::BOTTOM, Alignment::HCENTER));
  Utils::mapSetValue(m_alignmentByButton, alignBottomRightBtn, Alignment(Alignment::BOTTOM, Alignment::RIGHT));

  m_alignmentButtonGroup = new QButtonGroup(this);
  for (const auto& buttonAndAlignment : m_alignmentByButton) {
    m_alignmentButtonGroup->addButton(buttonAndAlignment.first);
  }

  // The same range as in the Deskew step.
  angleSpinBox->setRange(-45.0, 45.0);
  Utils::setNumberFieldWidth(angleSpinBox, -45.0);
  // The items follow the order of deskew::SkewDetection, as in the Deskew step.
  deskewDetectionCB->addItem(QCoreApplication::translate("deskew::OptionsWidget", "Content"));
  deskewDetectionCB->addItem(QCoreApplication::translate("deskew::OptionsWidget", "Top page edge"));


  depthPerceptionSlider->setMinimum(qRound(DepthPerception::minValue() * 10));
  depthPerceptionSlider->setMaximum(qRound(DepthPerception::maxValue() * 10));

  despeckleSlider->setMinimum(qRound(1.0 * 10));
  despeckleSlider->setMaximum(qRound(3.0 * 10));

  const int index = profileCB->findData(DefaultParamsProvider::getInstance().getProfileName());
  if (index != -1) {
    profileCB->setCurrentIndex(index);
  }
  profileChanged(profileCB->currentIndex());

  connect(buttonBox, &QDialogButtonBox::accepted, this, &DefaultParamsDialog::commitChanges);
  connect(binarizationOptions, &QStackedWidget::currentChanged, this,
          &DefaultParamsDialog::updateBinarizationPageSizes);
  updateBinarizationPageSizes();

  setupUiConnections();
}

void DefaultParamsDialog::updateBinarizationPageSizes() {
  // Only the page of the chosen method counts for the height, so the smoothing options follow
  // right below its settings. The widest page still counts for the width, so the window keeps
  // its width when the method changes.
  for (int i = 0; i < binarizationOptions->count(); ++i) {
    QWidget* methodPage = binarizationOptions->widget(i);
    const bool current = (i == binarizationOptions->currentIndex());
    methodPage->setSizePolicy(QSizePolicy::Preferred, current ? QSizePolicy::Preferred : QSizePolicy::Ignored);
  }
  binarizationOptions->updateGeometry();
}

void DefaultParamsDialog::showEvent(QShowEvent* event) {
  QDialog::showEvent(event);
  // Before the window appears on the screen.
  if (!m_fittedToContents) {
    m_fittedToContents = true;
    fitToContents();
  }
}

bool DefaultParamsDialog::eventFilter(QObject* watched, QEvent* event) {
  // Enter in the name field of Custom saves the profile instead of closing the dialog. Caught
  // before the field itself sees it, which would select an item of the same name.
  if ((event->type() == QEvent::KeyPress) && profileCB->isEditable() && (watched == profileCB->lineEdit())) {
    const int key = static_cast<QKeyEvent*>(event)->key();
    if ((key == Qt::Key_Return) || (key == Qt::Key_Enter)) {
      profileSavePressed();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void DefaultParamsDialog::fitToContents() {
  QSize available(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
  if (const QScreen* screen = this->screen()) {
    available = screen->availableGeometry().size() - QSize(40, 80);
  }

  // A scroll area asks for little space on its own, so the window would open smaller than its
  // contents. For the first layout, each one asks for the full size of its contents. The width
  // stays the minimum, with room for a vertical scroll bar, so only the height ever scrolls.
  // A tab that was never shown reports too small a size, so each one is shown once.
  // The Output tab, with its many filters, is much taller than the others. It gets one and a half
  // times the height of the tallest other tab and scrolls beyond that; the others never scroll.
  const int currentTab = tabWidget->currentIndex();
  const int outputTab = tabWidget->indexOf(tab_6);
  std::vector<QScrollArea*> scrollAreas;
  std::vector<QScrollArea*> outputScrollAreas;
  int otherTabsHeight = 0;
  for (int i = 0; i < tabWidget->count(); ++i) {
    tabWidget->setCurrentIndex(i);
    // Only once polished, with the style sheet applied, do the widgets report their final size;
    // measured earlier, the Output tab came out too narrow and got a horizontal scroll bar.
    tabWidget->widget(i)->ensurePolished();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    for (QScrollArea* area : tabWidget->widget(i)->findChildren<QScrollArea*>()) {
      QWidget* contents = area->widget();
      if (!contents) {
        continue;
      }
      const int frame = 2 * area->frameWidth();
      const int scrollBar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, area);
      const QSize hint = contents->sizeHint();
      area->setMinimumSize(std::min(hint.width() + frame + scrollBar, available.width()), hint.height() + frame);
      scrollAreas.push_back(area);
      if (i == outputTab) {
        outputScrollAreas.push_back(area);
      } else {
        otherTabsHeight = std::max(otherTabsHeight, hint.height() + frame);
      }
    }
  }
  for (QScrollArea* area : outputScrollAreas) {
    area->setMinimumHeight(std::min(area->minimumHeight(), otherTabsHeight * 3 / 2));
  }
  tabWidget->setCurrentIndex(currentTab);

  // The layouts take the new minimum sizes into account only with their pending layout requests,
  // otherwise the window grows later, after it has been centered.
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  layout()->invalidate();
  layout()->activate();
  // Not adjustSize(): it limits a window to two thirds of the screen.
  resize(sizeHint().expandedTo(minimumSizeHint()).boundedTo(available));
  // The dialog was centered for its earlier size. Center it again, with its title bar and frame,
  // and keep it on the screen.
  QMargins frame;
  if (const QWindow* window = windowHandle()) {
    frame = window->frameMargins();
  }
  const QSize frameSize = size() + QSize(frame.left() + frame.right(), frame.top() + frame.bottom());
  const QRect screenRect = this->screen() ? this->screen()->availableGeometry() : QRect();
  const QPoint center = parentWidget() ? parentWidget()->window()->frameGeometry().center() : screenRect.center();
  QPoint topLeft = center - QPoint(frameSize.width() / 2, frameSize.height() / 2);
  if (screenRect.isValid()) {
    topLeft.setX(std::max(screenRect.left(), std::min(topLeft.x(), screenRect.right() + 1 - frameSize.width())));
    topLeft.setY(std::max(screenRect.top(), std::min(topLeft.y(), screenRect.bottom() + 1 - frameSize.height())));
  }
  move(topLeft);

  // Afterwards the window may be made lower again, but not narrower.
  for (QScrollArea* area : scrollAreas) {
    area->setMinimumHeight(0);
  }
}

void DefaultParamsDialog::updateFixOrientationDisplay(const DefaultParams::FixOrientationParams& params) {
  m_orthogonalRotation = params.getImageRotation();
  setRotationPixmap();
}

void DefaultParamsDialog::updatePageSplitDisplay(const DefaultParams::PageSplitParams& params) {
  LayoutType layoutType = params.getLayoutType();
  if (layoutType == AUTO_LAYOUT_TYPE) {
    layoutAutoBtn->setChecked(true);
    pageLayoutGroup->setEnabled(false);
  } else {
    layoutManualBtn->setChecked(true);
    pageLayoutGroup->setEnabled(true);
  }

  switch (layoutType) {
    case AUTO_LAYOUT_TYPE:
      // Uncheck all buttons. Can only be done
      // by playing with exclusiveness.
      twoPagesBtn->setChecked(true);
      twoPagesBtn->setAutoExclusive(false);
      twoPagesBtn->setChecked(false);
      twoPagesBtn->setAutoExclusive(true);
      break;
    case SINGLE_PAGE_UNCUT:
      singlePageUncutBtn->setChecked(true);
      break;
    case PAGE_PLUS_OFFCUT:
      pagePlusOffcutBtn->setChecked(true);
      break;
    case TWO_PAGES:
      twoPagesBtn->setChecked(true);
      break;
  }

  if (params.getPageOrder() == Qt::RightToLeft) {
    rightFirstBtn->setChecked(true);
  } else {
    leftFirstBtn->setChecked(true);
  }
}

void DefaultParamsDialog::updateDeskewDisplay(const DefaultParams::DeskewParams& params) {
  AutoManualMode mode = params.getMode();
  if (mode == MODE_AUTO) {
    deskewAutoBtn->setChecked(true);
  } else {
    deskewManualBtn->setChecked(true);
  }
  angleSpinBox->setEnabled(mode == MODE_MANUAL);
  angleSpinBox->setValue(params.getDeskewAngleDeg());
  deskewDetectionLabel->setEnabled(mode == MODE_AUTO);
  deskewDetectionCB->setEnabled(mode == MODE_AUTO);
  deskewDetectionCB->setCurrentIndex(params.getDetection());
  if (params.isAutoOblique()) {
    deskewObliqueAutoBtn->setChecked(true);
  } else {
    deskewObliqueManualBtn->setChecked(true);
  }
}

void DefaultParamsDialog::updateSelectContentDisplay(const DefaultParams::SelectContentParams& params) {
  const AutoManualMode pageDetectMode = params.getPageDetectMode();
  pageDetectOptions->setEnabled(pageDetectMode != MODE_DISABLED);
  fineTuneBtn->setEnabled(pageDetectMode == MODE_AUTO);
  dimensionsWidget->setEnabled(pageDetectMode == MODE_MANUAL);
  switch (pageDetectMode) {
    case MODE_AUTO:
      pageDetectAutoBtn->setChecked(true);
      break;
    case MODE_MANUAL:
      pageDetectManualBtn->setChecked(true);
      break;
    case MODE_DISABLED:
      pageDetectDisableBtn->setChecked(true);
      break;
  }

  if (params.isContentDetectEnabled()) {
    contentDetectAutoBtn->setChecked(true);
  } else {
    contentDetectDisableBtn->setChecked(true);
  }
  // Program settings, not part of the profile.
  fillFactorSpinBox->setValue(ApplicationSettings::getInstance().getContentFillFactor());
  borderToleranceSpinBox->setValue(ApplicationSettings::getInstance().getContentBorderTolerance());
  contentDetectOptions->setEnabled(params.isContentDetectEnabled());

  QSizeF pageRectSize = params.getPageRectSize();
  widthSpinBox->setValue(pageRectSize.width());
  heightSpinBox->setValue(pageRectSize.height());
  fineTuneBtn->setChecked(params.isFineTuneCorners());
}

void DefaultParamsDialog::updatePageLayoutDisplay(const DefaultParams::PageLayoutParams& params) {
  autoMargins->setChecked(params.isAutoMargins());
  marginsWidget->setEnabled(!params.isAutoMargins());

  const page_layout::OutputSizeOptions& outputSizeOptions = params.getOutputSizeOptions();
  ownTargetSizeCB->setChecked(outputSizeOptions.hasOwnTargetSize());
  targetWidthSpinBox->setValue(outputSizeOptions.targetSizeMM().width());
  targetHeightSpinBox->setValue(outputSizeOptions.targetSizeMM().height());
  maxEnlargeSpinBox->setValue(outputSizeOptions.maxEnlargePercent());
  minReduceSpinBox->setValue(outputSizeOptions.minReducePercent());
  updateTargetSizeEnabled();

  const Margins& margins = params.getHardMargins();
  topMarginSpinBox->setValue(margins.top());
  rightMarginSpinBox->setValue(margins.right());
  bottomMarginSpinBox->setValue(margins.bottom());
  leftMarginSpinBox->setValue(margins.left());

  m_topBottomLinkEnabled = (margins.top() == margins.bottom());
  m_leftRightLinkEnabled = (margins.left() == margins.right());
  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);

  const Alignment& alignment = params.getAlignment();
  if (alignment.horizontal() == Alignment::HAUTO) {
    hAlignmentModeCB->setCurrentIndex(0);
  } else if (alignment.horizontal() == Alignment::HORIGINAL) {
    hAlignmentModeCB->setCurrentIndex(2);
  } else {
    hAlignmentModeCB->setCurrentIndex(1);
  }
  if (alignment.vertical() == Alignment::VAUTO) {
    vAlignmentModeCB->setCurrentIndex(0);
  } else if (alignment.vertical() == Alignment::VORIGINAL) {
    vAlignmentModeCB->setCurrentIndex(2);
  } else {
    vAlignmentModeCB->setCurrentIndex(1);
  }
  matchSizeModeCB->setCurrentIndex(alignment.isNull() ? 0 : (alignment.isScaleToFit() ? 2 : 1));

  for (const auto& [button, btnAlignment] : m_alignmentByButton) {
    if (alignment.isAutoVertical()) {
      if ((btnAlignment.vertical() == Alignment::VCENTER) && (btnAlignment.horizontal() == alignment.horizontal())) {
        button->setChecked(true);
        break;
      }
    } else if (alignment.isAutoHorizontal()) {
      if ((btnAlignment.horizontal() == Alignment::HCENTER) && (btnAlignment.vertical() == alignment.vertical())) {
        button->setChecked(true);
        break;
      }
    } else if (btnAlignment == alignment) {
      button->setChecked(true);
      break;
    }
  }

  updateAlignmentModeEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::updateOutputDisplay(const DefaultParams::OutputParams& params) {
  const ColorParams& colorParams = params.getColorParams();
  colorModeBWBtn->setChecked(colorParams.colorMode() == BLACK_AND_WHITE);
  colorModeColorBtn->setChecked(colorParams.colorMode() == COLOR_GRAYSCALE);
  colorModeMixedBtn->setChecked(colorParams.colorMode() == MIXED);
  // Program settings, not part of the profile.
  colorDetectionMinColorSB->setValue(ApplicationSettings::getInstance().getColorDetectionMinColor());
  colorDetectionZonesCB->setChecked(ApplicationSettings::getInstance().isColorDetectionZones());
  colorDetectionMinPictureSB->setValue(ApplicationSettings::getInstance().getColorDetectionMinPicture());
  colorDetectionMinPictureLabel->setEnabled(colorDetectionZonesCB->isChecked());
  colorDetectionMinPictureSB->setEnabled(colorDetectionZonesCB->isChecked());

  const ColorCommonOptions& colorCommonOptions = colorParams.colorCommonOptions();
  const BlackWhiteOptions& blackWhiteOptions = colorParams.blackWhiteOptions();
  fillMarginsCB->setChecked(colorCommonOptions.fillMargins());
  fillOffcutCB->setChecked(colorCommonOptions.fillOffcut());
  fillOutsidePageBoxCB->setChecked(colorCommonOptions.fillOutsidePageBox());
  m_bwNormalizeIllumination = blackWhiteOptions.normalizeIllumination();
  m_colorNormalizeIllumination = colorCommonOptions.normalizeIllumination();
  grayscaleOutputCB->setChecked(colorCommonOptions.isGrayscaleOutput());
  savitzkyGolaySmoothingCB->setChecked(blackWhiteOptions.isSavitzkyGolaySmoothingEnabled());
  morphologicalSmoothingCB->setChecked(blackWhiteOptions.isMorphologicalSmoothingEnabled());

  fillingColorBox->setCurrentIndex(fillingColorBox->findData(colorCommonOptions.getFillingColor()));

  // The strength shown while the denoiser is off is only kept for turning it on.
  chromaCB->setChecked(colorCommonOptions.chromaDenoise());
  chromaRadius->setValue(colorCommonOptions.chromaRadius());
  chromaGrayLimit->setValue(colorCommonOptions.chromaGrayLimit());
  const bool subtractBgOn = colorCommonOptions.subtractBackgroundCoef() > 0.0;
  if (subtractBgOn) {
    m_subtractBgCoefWhenOn = colorCommonOptions.subtractBackgroundCoef();
  }
  subtractBgCB->setChecked(subtractBgOn);
  subtractBgCoef->setValue(m_subtractBgCoefWhenOn);
  subtractBgRadius->setValue(colorCommonOptions.subtractBackgroundRadius());
  subtractBgWhiteCB->setChecked(colorCommonOptions.subtractBackgroundWhite());
  const bool knnOn = colorCommonOptions.knnCoef() > 0.0;
  if (knnOn) {
    m_knnCoefWhenOn = colorCommonOptions.knnCoef();
  }
  knnCB->setChecked(knnOn);
  knnCoef->setValue(m_knnCoefWhenOn);
  knnRadius->setValue(colorCommonOptions.knnRadius());
  const bool sharpenOn = colorCommonOptions.sharpenCoef() > 0.0;
  if (sharpenOn) {
    m_sharpenCoefWhenOn = colorCommonOptions.sharpenCoef();
  }
  sharpenCB->setChecked(sharpenOn);
  sharpenCoef->setValue(m_sharpenCoefWhenOn);
  sharpenRadius->setValue(colorCommonOptions.sharpenRadius());
  const bool wienerOn = colorCommonOptions.wienerCoef() > 0.0;
  if (wienerOn) {
    m_wienerCoefWhenOn = colorCommonOptions.wienerCoef();
  }
  wienerCB->setChecked(wienerOn);
  wienerCoef->setValue(m_wienerCoefWhenOn);
  wienerWindowSize->setValue(colorCommonOptions.wienerWindowSize());

  colorSegmentationCB->setChecked(blackWhiteOptions.getColorSegmenterOptions().isEnabled());
  reduceNoiseSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getNoiseReduction());
  redAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getRedThresholdAdjustment());
  greenAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getGreenThresholdAdjustment());
  blueAdjustmentSB->setValue(blackWhiteOptions.getColorSegmenterOptions().getBlueThresholdAdjustment());
  {
    const BlackWhiteOptions::ColorSegmenterOptions& segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
    m_textColorReduction = segmenterOptions.isKMeansEnabled() ? 2 : segmenterOptions.isPosterizeEnabled() ? 1 : 0;
    m_pagePosterize = colorCommonOptions.getPosterizationOptions().isEnabled();
    updateColorReductionControls();
    kmeansColorsSB->setValue(segmenterOptions.getKMeansColors());
    kmeansFixBlackCB->setChecked(segmenterOptions.isKMeansBlackFixed());
  }
  posterizeLevelSB->setValue(colorCommonOptions.getPosterizationOptions().getLevel());
  posterizeNormalizationCB->setChecked(colorCommonOptions.getPosterizationOptions().isNormalizationEnabled());
  posterizeForceBwCB->setChecked(colorCommonOptions.getPosterizationOptions().isForceBlackAndWhite());

  thresholdMethodBox->setCurrentIndex(thresholdMethodBox->findData(blackWhiteOptions.getBinarizationMethod()));
  m_binarizationMethod = blackWhiteOptions.getBinarizationMethod();
  for (QDoubleSpinBox* delta : {otsuDelta, sauvolaDelta, wolfDelta}) {
    delta->setValue(blackWhiteOptions.thresholdAdjustment());
  }
  sauvolaWindowSize->setValue(blackWhiteOptions.getWindowSize());
  wolfWindowSize->setValue(blackWhiteOptions.getWindowSize());
  sauvolaCoef->setValue(blackWhiteOptions.getSauvolaCoef());
  lowerBound->setValue(blackWhiteOptions.getWolfLowerBound());
  upperBound->setValue(blackWhiteOptions.getWolfUpperBound());
  coloredDimmingSB->setValue(blackWhiteOptions.getColoredDimming());
  wolfCoef->setValue(blackWhiteOptions.getWolfCoef());

  const PictureShapeOptions& shapeOptions = params.getPictureShapeOptions();
  pictureShapeSelector->setCurrentIndex(pictureShapeSelector->findData(shapeOptions.getPictureShape()));
  pictureShapeSensitivitySB->setValue(shapeOptions.getSensitivity());
  higherSearchSensitivityCB->setChecked(shapeOptions.isHigherSearchSensitivity());

  m_outputDpi = params.getDpi().vertical();
  dpiSelector->setEditText(Utils::dpiText(m_outputDpi));

  const SplittingOptions& splitOptions = params.getSplittingOptions();
  splittingCB->setChecked(splitOptions.isSplitOutput());
  switch (splitOptions.getSplittingMode()) {
    case BLACK_AND_WHITE_FOREGROUND:
      bwForegroundRB->setChecked(true);
      break;
    case COLOR_FOREGROUND:
      colorForegroundRB->setChecked(true);
      break;
  }
  originalBackgroundCB->setChecked(splitOptions.isOriginalBackgroundEnabled());

  const double despeckleLevel = params.getDespeckleLevel();
  if (despeckleLevel != 0) {
    despeckleCB->setChecked(true);
    despeckleSlider->setValue(qRound(10 * despeckleLevel));
  } else {
    despeckleCB->setChecked(false);
  }
  despeckleSlider->setToolTip(QLocale().toString(0.1 * despeckleSlider->value(), 'f', 1));
  despeckleValueLabel->setText(despeckleSlider->toolTip());

  if (QAbstractButton* dewarpingModeBtn = m_dewarpingModeGroup->button(params.getDewarpingOptions().dewarpingMode())) {
    dewarpingModeBtn->setChecked(true);
  }
  dewarpingPostDeskewCB->setChecked(params.getDewarpingOptions().needPostDeskew());
  evenOutLetterWidthsCB->setChecked(params.getDewarpingOptions().evenOutLetterWidths());
  depthPerceptionSlider->setValue(qRound(params.getDepthPerception().value() * 10));
  depthPerceptionValueLabel->setText(QLocale().toString(params.getDepthPerception().value(), 'f', 1));

  // update the display
  colorModeChanged(currentColorMode());
  thresholdMethodChanged(thresholdMethodBox->currentIndex());
  pictureShapeChanged(pictureShapeSelector->currentIndex());
  updateEqualizeIlluminationDisplay();
  splittingToggled(splittingCB->isChecked());
  despeckleToggled(despeckleCB->isChecked());
  chromaOptionsWidget->setEnabled(chromaCB->isChecked());
  subtractBgOptionsWidget->setEnabled(subtractBgOn);
  knnOptionsWidget->setEnabled(knnOn);
  sharpenOptionsWidget->setEnabled(sharpenOn);
  wienerOptionsWidget->setEnabled(wienerOn);
  dewarpingModeChanged();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void DefaultParamsDialog::setupUiConnections() {
  CONNECT(rotateLeftBtn, &QAbstractButton::clicked, this, &DefaultParamsDialog::rotateLeft);
  CONNECT(rotateRightBtn, &QAbstractButton::clicked, this, &DefaultParamsDialog::rotateRight);
  CONNECT(layoutManualBtn, &QAbstractButton::toggled, this, &DefaultParamsDialog::layoutModeToggled);
  CONNECT(deskewAutoBtn, &QAbstractButton::toggled, this, &DefaultParamsDialog::deskewModeChanged);
  CONNECT(pageDetectAutoBtn, &QAbstractButton::toggled, this, &DefaultParamsDialog::pageDetectAutoToggled);
  CONNECT(contentDetectAutoBtn, &QAbstractButton::toggled, contentDetectOptions, &QWidget::setEnabled);
  CONNECT(pageDetectManualBtn, &QAbstractButton::toggled, this, &DefaultParamsDialog::pageDetectManualToggled);
  CONNECT(pageDetectDisableBtn, &QAbstractButton::toggled, this, &DefaultParamsDialog::pageDetectDisableToggled);
  CONNECT(autoMargins, &QAbstractButton::toggled, this, &DefaultParamsDialog::autoMarginsToggled);
  CONNECT(hAlignmentModeCB, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::alignmentModeChanged);
  CONNECT(vAlignmentModeCB, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::alignmentModeChanged);
  CONNECT(matchSizeModeCB, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::matchSizeModeChanged);
  CONNECT(ownTargetSizeCB, &QAbstractButton::toggled, this, &DefaultParamsDialog::updateTargetSizeEnabled);
  CONNECT(topBottomLink, &QAbstractButton::clicked, this, &DefaultParamsDialog::topBottomLinkClicked);
  CONNECT(leftRightLink, &QAbstractButton::clicked, this, &DefaultParamsDialog::leftRightLinkClicked);
  CONNECT(topMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::vertMarginsChanged);
  CONNECT(bottomMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::vertMarginsChanged);
  CONNECT(leftMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::horMarginsChanged);
  CONNECT(rightMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::horMarginsChanged);
  CONNECT(colorDetectionZonesCB, &QAbstractButton::toggled, colorDetectionMinPictureLabel, &QWidget::setEnabled);
  CONNECT(colorDetectionZonesCB, &QAbstractButton::toggled, colorDetectionMinPictureSB, &QWidget::setEnabled);
  CONNECT(colorModeBWBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(BLACK_AND_WHITE); });
  CONNECT(colorModeColorBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(COLOR_GRAYSCALE); });
  CONNECT(colorModeMixedBtn, &QAbstractButton::clicked, this, [this] { colorModeChanged(MIXED); });
  CONNECT(thresholdMethodBox, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::thresholdMethodChanged);
  CONNECT(pictureShapeSelector, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::pictureShapeChanged);
  CONNECT(equalizeIlluminationCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::equalizeIlluminationToggled);
  CONNECT(equalizeIlluminationColorCB, &QAbstractButton::clicked, this,
          &DefaultParamsDialog::equalizeIlluminationColorToggled);
  CONNECT(splittingCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::splittingToggled);
  CONNECT(bwForegroundRB, &QAbstractButton::clicked, this, &DefaultParamsDialog::bwForegroundToggled);
  CONNECT(colorForegroundRB, &QAbstractButton::clicked, this, &DefaultParamsDialog::colorForegroundToggled);
  CONNECT(otsuDelta, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::thresholdDeltaChanged);
  CONNECT(sauvolaDelta, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::thresholdDeltaChanged);
  CONNECT(wolfDelta, &QDoubleSpinBox::valueChanged, this, &DefaultParamsDialog::thresholdDeltaChanged);
  CONNECT(dpiSelector, &QComboBox::activated, this, &DefaultParamsDialog::dpiSelectionChanged);
  CONNECT(dpiSelector->lineEdit(), &QLineEdit::editingFinished, this, &DefaultParamsDialog::dpiSelectionChanged);
  CONNECT(fillOffcutCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::fillOffcutToggled);
  CONNECT(fillOutsidePageBoxCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::fillOutsidePageBoxToggled);
  CONNECT(chromaCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::chromaToggled);
  CONNECT(subtractBgCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::subtractBgToggled);
  CONNECT(knnCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::knnToggled);
  CONNECT(sharpenCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::sharpenToggled);
  CONNECT(wienerCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::wienerToggled);
  CONNECT(m_dewarpingModeGroup, &QButtonGroup::idClicked, this, &DefaultParamsDialog::dewarpingModeChanged);
  CONNECT(depthPerceptionSlider, &QAbstractSlider::valueChanged, this,
          &DefaultParamsDialog::depthPerceptionChangedSlot);
  CONNECT(profileCB, &QComboBox::currentIndexChanged, this, &DefaultParamsDialog::profileChanged);
  CONNECT(profileSaveButton, &QAbstractButton::clicked, this, &DefaultParamsDialog::profileSavePressed);
  CONNECT(profileDeleteButton, &QAbstractButton::clicked, this, &DefaultParamsDialog::profileDeletePressed);
  CONNECT(colorSegmentationCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::colorSegmentationToggled);
  CONNECT(colorReductionCB, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &DefaultParamsDialog::colorReductionChanged);
  CONNECT(despeckleCB, &QAbstractButton::clicked, this, &DefaultParamsDialog::despeckleToggled);
  CONNECT(despeckleSlider, &QAbstractSlider::valueChanged, this, &DefaultParamsDialog::despeckleSliderValueChanged);
}

#undef CONNECT

void DefaultParamsDialog::rotateLeft() {
  OrthogonalRotation rotation(m_orthogonalRotation);
  rotation.prevClockwiseDirection();
  setRotation(rotation);
}

void DefaultParamsDialog::rotateRight() {
  OrthogonalRotation rotation(m_orthogonalRotation);
  rotation.nextClockwiseDirection();
  setRotation(rotation);
}

void DefaultParamsDialog::setRotation(const OrthogonalRotation& rotation) {
  if (rotation == m_orthogonalRotation) {
    return;
  }

  m_orthogonalRotation = rotation;
  setRotationPixmap();
}

void DefaultParamsDialog::setRotationPixmap() {
  QIcon icon;
  switch (m_orthogonalRotation.toDegrees()) {
    case 0:
      icon = IconProvider::getInstance().getIcon("big-up-arrow");
      break;
    case 90:
      icon = IconProvider::getInstance().getIcon("big-right-arrow");
      break;
    case 180:
      icon = IconProvider::getInstance().getIcon("big-down-arrow");
      break;
    case 270:
      icon = IconProvider::getInstance().getIcon("big-left-arrow");
      break;
    default:
      assert(!"Unreachable");
  }

  rotationIndicator->setPixmap(icon.pixmap(QSize(32, 32)));
}

void DefaultParamsDialog::layoutModeToggled(const bool manual) {
  if (!manual) {
    // Uncheck all buttons. Can only be done
    // by playing with exclusiveness.
    twoPagesBtn->setChecked(true);
    twoPagesBtn->setAutoExclusive(false);
    twoPagesBtn->setChecked(false);
    twoPagesBtn->setAutoExclusive(true);
  } else {
    singlePageUncutBtn->setChecked(true);
  }
  pageLayoutGroup->setEnabled(manual);
}

void DefaultParamsDialog::deskewModeChanged(const bool autoMode) {
  angleSpinBox->setEnabled(!autoMode);
  deskewDetectionLabel->setEnabled(autoMode);
  deskewDetectionCB->setEnabled(autoMode);
}

void DefaultParamsDialog::pageDetectAutoToggled(const bool checked) {
  if (!checked) {
    return;  // The button checked instead does the work.
  }
  pageDetectOptions->setEnabled(true);
  fineTuneBtn->setEnabled(true);
  dimensionsWidget->setEnabled(false);
}

void DefaultParamsDialog::pageDetectManualToggled(const bool checked) {
  if (!checked) {
    return;
  }
  pageDetectOptions->setEnabled(true);
  fineTuneBtn->setEnabled(false);
  dimensionsWidget->setEnabled(true);
}

void DefaultParamsDialog::pageDetectDisableToggled(const bool checked) {
  if (!checked) {
    return;
  }
  pageDetectOptions->setEnabled(false);
}

void DefaultParamsDialog::autoMarginsToggled(const bool checked) {
  marginsWidget->setEnabled(!checked);
}

void DefaultParamsDialog::alignmentModeChanged(int) {
  updateAlignmentButtonsEnabled();
  updateAutoModeButtons();
}

void DefaultParamsDialog::updateTargetSizeEnabled() {
  const bool own = ownTargetSizeCB->isChecked();
  targetSizeLabel->setEnabled(own);
  targetWidthSpinBox->setEnabled(own);
  targetSizeTimesLabel->setEnabled(own);
  targetHeightSpinBox->setEnabled(own);
}

void DefaultParamsDialog::matchSizeModeChanged(int) {
  updateAlignmentModeEnabled();
}

ColorMode DefaultParamsDialog::currentColorMode() const {
  if (colorModeMixedBtn->isChecked()) {
    return MIXED;
  }
  return colorModeColorBtn->isChecked() ? COLOR_GRAYSCALE : BLACK_AND_WHITE;
}

void DefaultParamsDialog::colorModeChanged(const ColorMode colorMode) {
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
  thresholdOptions->setEnabled(thresholdOptionsVisible);
  despecklePanel->setEnabled(thresholdOptionsVisible);
  pictureShapeOptions->setEnabled(pictureShapeVisible);
  splittingOptions->setEnabled(splittingOptionsVisible);

  // The areas to fill are used in every color mode, the color not in black and white.
  fillingColorLabel->setEnabled(colorMode != BLACK_AND_WHITE);
  fillingColorBox->setEnabled(colorMode != BLACK_AND_WHITE);

  updateEqualizeIlluminationDisplay();
  grayscaleOutputCB->setEnabled(colorMode != BLACK_AND_WHITE);
  savitzkyGolaySmoothingCB->setEnabled(colorMode != COLOR_GRAYSCALE);
  morphologicalSmoothingCB->setEnabled(colorMode != COLOR_GRAYSCALE);

  colorSegmentationCB->setEnabled(thresholdOptionsVisible);
  updateColorReductionControls();
}

void DefaultParamsDialog::thresholdMethodChanged(const int idx) {
  const auto method = static_cast<BinarizationMethod>(thresholdMethodBox->itemData(idx).toInt());
  if (needsOwnBinarizationDefaults(static_cast<BinarizationMethod>(m_binarizationMethod), method)) {
    BlackWhiteOptions defaults;
    defaults.setBinarizationMethod(method);
    applyBinarizationDefaults(defaults);
    sauvolaWindowSize->setValue(defaults.getWindowSize());
    wolfWindowSize->setValue(defaults.getWindowSize());
    sauvolaCoef->setValue(defaults.getSauvolaCoef());
  }
  m_binarizationMethod = method;

  // The methods share three pages of settings, as in the Output step.
  switch (method) {
    case T_SAUVOLA:
    case T_BRADLEY:
    case T_EDGEPLUS:
    case T_BLURDIV:
    case T_EDGEDIV:
    case T_GATOS:
    case T_NICK:
    case T_SINGH:
    case T_ROBUST:
    case T_MULTISCALE:
      binarizationOptions->setCurrentWidget(page_2);
      break;
    case T_WOLF:
    case T_FOX:
    case T_WINDOW:
    case T_GRAD:
      binarizationOptions->setCurrentWidget(page_3);
      break;
    default:
      binarizationOptions->setCurrentWidget(page);
      break;
  }
}

void DefaultParamsDialog::pictureShapeChanged(const int idx) {
  const auto shapeMode = static_cast<PictureShape>(pictureShapeSelector->itemData(idx).toInt());
  pictureShapeSensitivityOptions->setEnabled(shapeMode == RECTANGULAR_SHAPE);
  higherSearchSensitivityCB->setEnabled(shapeMode != OFF_SHAPE);
}

void DefaultParamsDialog::equalizeIlluminationToggled(const bool checked) {
  // As in the Output step: the color setting in color mode, otherwise the black and white one.
  const auto colorMode = currentColorMode();
  if (colorMode == COLOR_GRAYSCALE) {
    m_colorNormalizeIllumination = checked;
  } else {
    m_bwNormalizeIllumination = checked;
    if ((colorMode == MIXED) && !checked) {
      m_colorNormalizeIllumination = false;
    }
  }
  updateEqualizeIlluminationDisplay();
}

void DefaultParamsDialog::equalizeIlluminationColorToggled(const bool checked) {
  m_colorNormalizeIllumination = checked;
}

void DefaultParamsDialog::updateEqualizeIlluminationDisplay() {
  const auto colorMode = currentColorMode();
  const QSignalBlocker blocker(equalizeIlluminationCB);
  const QSignalBlocker colorBlocker(equalizeIlluminationColorCB);
  equalizeIlluminationCB->setChecked((colorMode == COLOR_GRAYSCALE) ? m_colorNormalizeIllumination
                                                                    : m_bwNormalizeIllumination);
  equalizeIlluminationColorCB->setChecked(m_colorNormalizeIllumination);
  equalizeIlluminationColorCB->setEnabled((colorMode == MIXED) && m_bwNormalizeIllumination);
}

void DefaultParamsDialog::splittingToggled(const bool checked) {
  bwForegroundRB->setEnabled(checked);
  colorForegroundRB->setEnabled(checked);
  originalBackgroundCB->setEnabled(checked && bwForegroundRB->isChecked());
}

void DefaultParamsDialog::bwForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }
  originalBackgroundCB->setEnabled(checked);
}

void DefaultParamsDialog::colorForegroundToggled(bool checked) {
  if (!checked) {
    return;
  }
  originalBackgroundCB->setEnabled(!checked);
}

void DefaultParamsDialog::loadParams(const DefaultParams& params) {
  auto block = m_connectionManager.getScopedBlock();

  // must be done before updating the displays in order to set the precise of the spin boxes
  updateUnits(params.getUnits());

  updateFixOrientationDisplay(params.getFixOrientationParams());
  updatePageSplitDisplay(params.getPageSplitParams());
  updateDeskewDisplay(params.getDeskewParams());
  updateSelectContentDisplay(params.getSelectContentParams());
  updatePageLayoutDisplay(params.getPageLayoutParams());
  updateOutputDisplay(params.getOutputParams());
}

std::unique_ptr<DefaultParams> DefaultParamsDialog::buildParams() const {
  DefaultParams::FixOrientationParams fixOrientationParams(m_orthogonalRotation);

  LayoutType layoutType;
  if (layoutAutoBtn->isChecked()) {
    layoutType = AUTO_LAYOUT_TYPE;
  } else if (singlePageUncutBtn->isChecked()) {
    layoutType = SINGLE_PAGE_UNCUT;
  } else if (pagePlusOffcutBtn->isChecked()) {
    layoutType = PAGE_PLUS_OFFCUT;
  } else {
    layoutType = TWO_PAGES;
  }
  DefaultParams::PageSplitParams pageSplitParams(layoutType);
  pageSplitParams.setPageOrder(rightFirstBtn->isChecked() ? Qt::RightToLeft : Qt::LeftToRight);

  DefaultParams::DeskewParams deskewParams(angleSpinBox->value(), deskewAutoBtn->isChecked() ? MODE_AUTO : MODE_MANUAL,
                                           deskewObliqueAutoBtn->isChecked());
  deskewParams.setDetection(static_cast<deskew::SkewDetection>(deskewDetectionCB->currentIndex()));

  const AutoManualMode pageBoxMode = pageDetectDisableBtn->isChecked()  ? MODE_DISABLED
                                     : pageDetectManualBtn->isChecked() ? MODE_MANUAL
                                                                        : MODE_AUTO;
  DefaultParams::SelectContentParams selectContentParams(QSizeF(widthSpinBox->value(), heightSpinBox->value()),
                                                         !contentDetectDisableBtn->isChecked(), pageBoxMode,
                                                         fineTuneBtn->isChecked());

  Alignment alignment;
  switch (hAlignmentModeCB->currentIndex()) {
    case 0:
      alignment.setHorizontal(Alignment::HAUTO);
      break;
    case 1:
      alignment.setHorizontal(m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal());
      break;
    case 2:
      alignment.setHorizontal(Alignment::HORIGINAL);
      break;
    default:
      break;
  }
  switch (vAlignmentModeCB->currentIndex()) {
    case 0:
      alignment.setVertical(Alignment::VAUTO);
      break;
    case 1:
      alignment.setVertical(m_alignmentByButton.at(getCheckedAlignmentButton()).vertical());
      break;
    case 2:
      alignment.setVertical(Alignment::VORIGINAL);
      break;
    default:
      break;
  }
  alignment.setNull(matchSizeModeCB->currentIndex() == 0);
  alignment.setScaleToFit(matchSizeModeCB->currentIndex() == 2);

  DefaultParams::PageLayoutParams pageLayoutParams(Margins(leftMarginSpinBox->value(), topMarginSpinBox->value(),
                                                           rightMarginSpinBox->value(), bottomMarginSpinBox->value()),
                                                   alignment, autoMargins->isChecked());
  page_layout::OutputSizeOptions outputSizeOptions;
  outputSizeOptions.setOwnTargetSize(ownTargetSizeCB->isChecked());
  // In the units of the profile, like the margins.
  outputSizeOptions.setTargetSizeMM(QSizeF(targetWidthSpinBox->value(), targetHeightSpinBox->value()));
  outputSizeOptions.setMaxEnlargePercent(maxEnlargeSpinBox->value());
  outputSizeOptions.setMinReducePercent(minReduceSpinBox->value());
  pageLayoutParams.setOutputSizeOptions(outputSizeOptions);

  const int dpi = m_outputDpi;
  ColorParams colorParams;
  colorParams.setColorMode(currentColorMode());

  ColorCommonOptions colorCommonOptions;
  colorCommonOptions.setFillingColor(static_cast<FillingColor>(fillingColorBox->currentData().toInt()));
  colorCommonOptions.setFillMargins(fillMarginsCB->isChecked());
  colorCommonOptions.setFillOffcut(fillOffcutCB->isChecked());
  colorCommonOptions.setFillOutsidePageBox(fillOutsidePageBoxCB->isChecked());
  colorCommonOptions.setChromaDenoise(chromaCB->isChecked());
  colorCommonOptions.setChromaRadius(chromaRadius->value());
  colorCommonOptions.setChromaGrayLimit(chromaGrayLimit->value());
  colorCommonOptions.setSubtractBackgroundCoef(subtractBgCB->isChecked() ? subtractBgCoef->value() : 0.0);
  colorCommonOptions.setSubtractBackgroundRadius(subtractBgRadius->value());
  colorCommonOptions.setSubtractBackgroundWhite(subtractBgWhiteCB->isChecked());
  colorCommonOptions.setKnnCoef(knnCB->isChecked() ? knnCoef->value() : 0.0);
  colorCommonOptions.setKnnRadius(knnRadius->value());
  colorCommonOptions.setSharpenCoef(sharpenCB->isChecked() ? sharpenCoef->value() : 0.0);
  colorCommonOptions.setSharpenRadius(sharpenRadius->value());
  colorCommonOptions.setWienerCoef(wienerCB->isChecked() ? wienerCoef->value() : 0.0);
  colorCommonOptions.setWienerWindowSize(wienerWindowSize->value());
  colorCommonOptions.setNormalizeIllumination(m_colorNormalizeIllumination);
  colorCommonOptions.setGrayscaleOutput(grayscaleOutputCB->isChecked());
  ColorCommonOptions::PosterizationOptions posterizationOptions = colorCommonOptions.getPosterizationOptions();
  posterizationOptions.setEnabled(m_pagePosterize);
  posterizationOptions.setLevel(posterizeLevelSB->value());
  posterizationOptions.setNormalizationEnabled(posterizeNormalizationCB->isChecked());
  posterizationOptions.setForceBlackAndWhite(posterizeForceBwCB->isChecked());
  colorCommonOptions.setPosterizationOptions(posterizationOptions);
  colorParams.setColorCommonOptions(colorCommonOptions);

  BlackWhiteOptions blackWhiteOptions;
  blackWhiteOptions.setNormalizeIllumination(m_bwNormalizeIllumination);
  blackWhiteOptions.setSavitzkyGolaySmoothingEnabled(savitzkyGolaySmoothingCB->isChecked());
  blackWhiteOptions.setMorphologicalSmoothingEnabled(morphologicalSmoothingCB->isChecked());
  BinarizationMethod binarizationMethod = static_cast<BinarizationMethod>(thresholdMethodBox->currentData().toInt());
  blackWhiteOptions.setBinarizationMethod(binarizationMethod);
  blackWhiteOptions.setThresholdAdjustment(static_cast<int>(otsuDelta->value()));
  blackWhiteOptions.setSauvolaCoef(sauvolaCoef->value());
  if (binarizationMethod == T_SAUVOLA || binarizationMethod == T_BRADLEY || binarizationMethod == T_EDGEPLUS
      || binarizationMethod == T_BLURDIV || binarizationMethod == T_EDGEDIV || binarizationMethod >= T_GATOS) {
    blackWhiteOptions.setWindowSize(sauvolaWindowSize->value());
  } else if (binarizationMethod == T_WOLF || binarizationMethod == T_FOX || binarizationMethod == T_WINDOW
             || binarizationMethod == T_GRAD) {
    blackWhiteOptions.setWindowSize(wolfWindowSize->value());
  }
  blackWhiteOptions.setWolfCoef(wolfCoef->value());
  blackWhiteOptions.setWolfUpperBound(upperBound->value());
  blackWhiteOptions.setWolfLowerBound(lowerBound->value());
  blackWhiteOptions.setColoredDimming(coloredDimmingSB->value());
  BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
  segmenterOptions.setEnabled(colorSegmentationCB->isChecked());
  segmenterOptions.setNoiseReduction(reduceNoiseSB->value());
  segmenterOptions.setRedThresholdAdjustment(redAdjustmentSB->value());
  segmenterOptions.setGreenThresholdAdjustment(greenAdjustmentSB->value());
  segmenterOptions.setBlueThresholdAdjustment(blueAdjustmentSB->value());
  segmenterOptions.setPosterizeEnabled(m_textColorReduction == 1);
  segmenterOptions.setKMeansEnabled(m_textColorReduction == 2);
  segmenterOptions.setKMeansColors(kmeansColorsSB->value());
  segmenterOptions.setKMeansBlackFixed(kmeansFixBlackCB->isChecked());
  blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  colorParams.setBlackWhiteOptions(blackWhiteOptions);

  SplittingOptions splitOptions;
  splitOptions.setSplitOutput(splittingCB->isChecked());
  splitOptions.setSplittingMode(bwForegroundRB->isChecked() ? BLACK_AND_WHITE_FOREGROUND : COLOR_FOREGROUND);
  splitOptions.setOriginalBackgroundEnabled(originalBackgroundCB->isChecked());

  PictureShapeOptions shapeOptions;
  shapeOptions.setPictureShape(static_cast<PictureShape>(pictureShapeSelector->currentData().toInt()));
  shapeOptions.setSensitivity(pictureShapeSensitivitySB->value());
  shapeOptions.setHigherSearchSensitivity(higherSearchSensitivityCB->isChecked());

  DewarpingOptions dewarpingOptions;
  dewarpingOptions.setDewarpingMode(static_cast<DewarpingMode>(m_dewarpingModeGroup->checkedId()));
  dewarpingOptions.setPostDeskew(dewarpingPostDeskewCB->isChecked());
  dewarpingOptions.setEvenOutLetterWidths(evenOutLetterWidthsCB->isChecked());

  double despeckleLevel;
  if (despeckleCB->isChecked()) {
    despeckleLevel = 0.1 * despeckleSlider->value();
  } else {
    despeckleLevel = 0;
  }

  DefaultParams::OutputParams outputParams(Dpi(dpi, dpi), colorParams, splitOptions, shapeOptions,
                                           DepthPerception(0.1 * depthPerceptionSlider->value()), dewarpingOptions,
                                           despeckleLevel);

  std::unique_ptr<DefaultParams> defaultParams = std::make_unique<DefaultParams>(
      fixOrientationParams, deskewParams, pageSplitParams, selectContentParams, pageLayoutParams, outputParams);
  defaultParams->setUnits(m_currentUnits);
  return defaultParams;
}

void DefaultParamsDialog::updateUnits(const Units units) {
  m_currentUnits = units;
  unitsLabel->setText(unitsToLocalizedString(units));

  // Sizes, margins and target sizes alike; the unit is shown in the fields.
  const bool coarse = (units == PIXELS) || (units == MILLIMETRES);
  const QString unitSuffix = QChar(' ') + unitsToLocalizedString(units);
  for (QDoubleSpinBox* spinBox : {widthSpinBox, heightSpinBox, topMarginSpinBox, bottomMarginSpinBox, leftMarginSpinBox,
                                  rightMarginSpinBox, targetWidthSpinBox, targetHeightSpinBox}) {
    spinBox->setDecimals(coarse ? 1 : 2);
    spinBox->setSingleStep(coarse ? 1.0 : 0.01);
    spinBox->setSuffix(unitSuffix);
  }
  // As in the side bars: three digits before the decimal point, sizes in pixels four.
  const double widestMargin = coarse ? 999.9 : 99.99;
  const double widestSize = (units == PIXELS) ? 9999.9 : widestMargin;
  for (QDoubleSpinBox* spinBox : {topMarginSpinBox, bottomMarginSpinBox, leftMarginSpinBox, rightMarginSpinBox}) {
    Utils::setNumberFieldWidth(spinBox, widestMargin);
  }
  for (QDoubleSpinBox* spinBox : {widthSpinBox, heightSpinBox, targetWidthSpinBox, targetHeightSpinBox}) {
    Utils::setNumberFieldWidth(spinBox, widestSize);
  }
}

void DefaultParamsDialog::setLinkButtonLinked(QToolButton* button, bool linked) {
  button->setIcon(linked ? m_chainIcon : m_brokenChainIcon);
}

void DefaultParamsDialog::topBottomLinkClicked() {
  m_topBottomLinkEnabled = !m_topBottomLinkEnabled;
  setLinkButtonLinked(topBottomLink, m_topBottomLinkEnabled);
  if (m_topBottomLinkEnabled && (topMarginSpinBox->value() != bottomMarginSpinBox->value())) {
    const double newMargin = std::min(topMarginSpinBox->value(), bottomMarginSpinBox->value());
    topMarginSpinBox->setValue(newMargin);
    bottomMarginSpinBox->setValue(newMargin);
  }
}

void DefaultParamsDialog::leftRightLinkClicked() {
  m_leftRightLinkEnabled = !m_leftRightLinkEnabled;
  setLinkButtonLinked(leftRightLink, m_leftRightLinkEnabled);
  if (m_leftRightLinkEnabled && (leftMarginSpinBox->value() != rightMarginSpinBox->value())) {
    const double newMargin = std::min(leftMarginSpinBox->value(), rightMarginSpinBox->value());
    leftMarginSpinBox->setValue(newMargin);
    rightMarginSpinBox->setValue(newMargin);
  }
}

void DefaultParamsDialog::horMarginsChanged(double val) {
  if (m_leftRightLinkEnabled) {
    auto block = m_connectionManager.getScopedBlock();
    leftMarginSpinBox->setValue(val);
    rightMarginSpinBox->setValue(val);
  }
}

void DefaultParamsDialog::vertMarginsChanged(double val) {
  if (m_topBottomLinkEnabled) {
    auto block = m_connectionManager.getScopedBlock();
    topMarginSpinBox->setValue(val);
    bottomMarginSpinBox->setValue(val);
  }
}

void DefaultParamsDialog::thresholdDeltaChanged(const double value) {
  // "Delta" is the same value for all methods, as in the Output step.
  for (QDoubleSpinBox* delta : {otsuDelta, sauvolaDelta, wolfDelta}) {
    if (delta != sender()) {
      const QSignalBlocker blocker(delta);
      delta->setValue(value);
    }
  }
}

void DefaultParamsDialog::dpiSelectionChanged() {
  // The message box takes the focus, which finishes the editing a second time.
  if (m_checkingDpi) {
    return;
  }
  bool ok = false;
  const int dpi = Utils::dpiFromText(dpiSelector->currentText(), &ok);
  if (!ok || (dpi < 72) || (dpi > 1200)) {
    m_checkingDpi = true;
    QMessageBox::warning(this, tr("Output Resolution"), tr("The resolution must be between 72 and 1200 DPI."));
    dpiSelector->setEditText(Utils::dpiText(m_outputDpi));
    m_checkingDpi = false;
    return;
  }
  m_outputDpi = dpi;
}

void DefaultParamsDialog::fillOffcutToggled(const bool checked) {
  // Filling the offcut and filling outside the page box exclude each other, as in the Output step.
  if (checked) {
    fillOutsidePageBoxCB->setChecked(false);
  }
}

void DefaultParamsDialog::fillOutsidePageBoxToggled(const bool checked) {
  if (checked) {
    fillOffcutCB->setChecked(false);
  }
}

void DefaultParamsDialog::chromaToggled(const bool checked) {
  chromaOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::subtractBgToggled(const bool checked) {
  subtractBgOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::knnToggled(const bool checked) {
  knnOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::sharpenToggled(const bool checked) {
  sharpenOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::wienerToggled(const bool checked) {
  wienerOptionsWidget->setEnabled(checked);
}

void DefaultParamsDialog::dewarpingModeChanged() {
  const int mode = m_dewarpingModeGroup->checkedId();
  dewarpingPostDeskewCB->setEnabled((mode == MANUAL) || (mode == MARGINAL));
  depthPerceptionPanel->setEnabled(mode != OFF);
  evenOutLetterWidthsCB->setEnabled(mode != OFF);
}

void DefaultParamsDialog::depthPerceptionChangedSlot(const int val) {
  const QString tooltipText(QLocale().toString(0.1 * val, 'f', 1));
  depthPerceptionSlider->setToolTip(tooltipText);
  depthPerceptionValueLabel->setText(tooltipText);

  // Show the tooltip immediately.
  const QPoint center(depthPerceptionSlider->rect().center());
  QPoint tooltipPos(depthPerceptionSlider->mapFromGlobal(QCursor::pos()));
  tooltipPos.setY(center.y());
  tooltipPos.setX(qBound(0, tooltipPos.x(), depthPerceptionSlider->width()));
  tooltipPos = depthPerceptionSlider->mapToGlobal(tooltipPos);
  QToolTip::showText(tooltipPos, tooltipText, depthPerceptionSlider);
}

void DefaultParamsDialog::profileChanged(const int index) {
  profileCB->setEditable(index == m_customProfileItemIdx);
  if (index == m_customProfileItemIdx) {
    profileCB->setEditText(profileCB->currentText());
    // The field is created anew each time the box becomes editable.
    profileCB->lineEdit()->installEventFilter(this);
    profileCB->lineEdit()->selectAll();
    profileCB->setFocus();
    profileSaveButton->setEnabled(true);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(true);

    if (DefaultParamsProvider::getInstance().getProfileName() == "Custom") {
      loadParams(DefaultParamsProvider::getInstance().getParams());
    } else {
      updateUnits(UnitsProvider::getInstance().getUnits());
    }
  } else if (profileCB->currentData().toString() == "Default") {
    profileSaveButton->setEnabled(false);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(false);

    std::unique_ptr<DefaultParams> defaultProfile = m_profileManager.createDefaultProfile();
    loadParams(*defaultProfile);
  } else if (profileCB->currentData().toString() == "Minimal") {
    profileSaveButton->setEnabled(false);
    profileDeleteButton->setEnabled(false);
    setTabWidgetsEnabled(false);

    std::unique_ptr<DefaultParams> minimalProfile = m_profileManager.createMinimalProfile();
    loadParams(*minimalProfile);
  } else {
    const QString profileName = profileCB->currentData().toString();
    DefaultParamsProfileManager::LoadStatus loadStatus;

    std::unique_ptr<DefaultParams> profile = m_profileManager.readProfile(profileName, &loadStatus);
    if (loadStatus == DefaultParamsProfileManager::SUCCESS) {
      profileSaveButton->setEnabled(true);
      profileDeleteButton->setEnabled(true);
      setTabWidgetsEnabled(true);

      loadParams(*profile);
    } else {
      if (loadStatus == DefaultParamsProfileManager::INCOMPATIBLE_VERSION_ERROR) {
        const int result = QMessageBox::warning(
            this, tr("Error"), tr("The profile file is not compatible with the current application version. Remove?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (result == QMessageBox::Yes) {
          m_profileManager.deleteProfile(profileName);
        }
      } else {
        QMessageBox::critical(this, tr("Error"), tr("Error loading the profile."));
      }
      profileCB->setCurrentIndex(0);
      profileCB->removeItem(index);
      m_customProfileItemIdx--;

      profileSaveButton->setEnabled(false);
      profileDeleteButton->setEnabled(false);
      setTabWidgetsEnabled(false);

      std::unique_ptr<DefaultParams> defaultProfile = m_profileManager.createDefaultProfile();
      loadParams(*defaultProfile);
    }
  }
}

void DefaultParamsDialog::profileSavePressed() {
  auto block = m_connectionManager.getScopedBlock();

  if (isProfileNameReserved(profileCB->currentText())) {
    QMessageBox::information(this, tr("Error"),
                             tr("The name conflicts with a default profile name. Please enter a different name."));
    return;
  }

  int existingIdx = -1;
  if (profileCB->currentIndex() == m_customProfileItemIdx) {
    const QString name = profileCB->currentText();
    // The name becomes a file name in the profile folder.
    static const QRegularExpression invalidChars(R"([\\/:*?"<>|])");
    if (name.trimmed().isEmpty() || name.startsWith('.') || name.contains(invalidChars)) {
      QMessageBox::information(this, tr("Error"),
                               tr("Please enter a name that doesn't contain any of the characters \\ / : * ? \" < > | "
                                  "and doesn't start with a dot."));
      return;
    }
    // File names don't depend on case on Windows and macOS.
    for (int i = 0; i < m_customProfileItemIdx; ++i) {
      const QString entry = profileCB->itemData(i).toString();
      if ((entry != "Default") && (entry != "Minimal") && (entry.compare(name, Qt::CaseInsensitive) == 0)) {
        existingIdx = i;
        break;
      }
    }
    if ((existingIdx != -1)
        && (QMessageBox::question(this, tr("Save Profile"),
                                  tr("A profile named \"%1\" already exists. Replace it?").arg(name),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)) {
      return;
    }
  }

  const QString fileName = (existingIdx != -1) ? profileCB->itemData(existingIdx).toString() : profileCB->currentText();
  if (m_profileManager.writeProfile(*buildParams(), fileName)) {
    if (existingIdx != -1) {
      // The existing entry shows the profile now, Custom keeps its name.
      profileCB->setCurrentIndex(existingIdx);
      profileCB->setEditable(false);
      profileDeleteButton->setEnabled(true);
    } else if (profileCB->currentIndex() == m_customProfileItemIdx) {
      const QString profileName = profileCB->currentText();
      profileCB->setItemData(profileCB->currentIndex(), profileName);
      profileCB->setItemText(profileCB->currentIndex(), profileName);
      m_customProfileItemIdx = profileCB->count();
      profileCB->addItem(tr("Custom"), "Custom");

      profileCB->setEditable(false);
      profileDeleteButton->setEnabled(true);
    }
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Error saving the profile."));
  }
}

void DefaultParamsDialog::profileDeletePressed() {
  if (m_profileManager.deleteProfile(profileCB->currentText())) {
    {
      auto block = m_connectionManager.getScopedBlock();

      const int deletedProfileIndex = profileCB->currentIndex();
      profileCB->setCurrentIndex(m_customProfileItemIdx--);
      profileCB->removeItem(deletedProfileIndex);
    }
    profileChanged(m_customProfileItemIdx);
  } else {
    QMessageBox::critical(this, tr("Error"), tr("Error deleting the profile."));
  }
}

bool DefaultParamsDialog::isProfileNameReserved(const QString& name) {
  return m_reservedProfileNames.find(name) != m_reservedProfileNames.end();
}

void DefaultParamsDialog::commitChanges() {
  const QString profile = profileCB->currentData().toString();
  std::unique_ptr<DefaultParams> params;
  if (profile == "Default") {
    params = m_profileManager.createDefaultProfile();
  } else if (profile == "Minimal") {
    params = m_profileManager.createMinimalProfile();
  } else {
    params = buildParams();
  }
  DefaultParamsProvider::getInstance().setParams(std::move(params), profile);
  ApplicationSettings::getInstance().setCurrentProfile(profile);
  ApplicationSettings::getInstance().setColorDetectionMinColor(colorDetectionMinColorSB->value());
  ApplicationSettings::getInstance().setColorDetectionZones(colorDetectionZonesCB->isChecked());
  ApplicationSettings::getInstance().setColorDetectionMinPicture(colorDetectionMinPictureSB->value());
  ApplicationSettings::getInstance().setContentFillFactor(fillFactorSpinBox->value());
  ApplicationSettings::getInstance().setContentBorderTolerance(borderToleranceSpinBox->value());
}

void DefaultParamsDialog::setTabWidgetsEnabled(const bool enabled) {
  for (int i = 0; i < tabWidget->count(); i++) {
    QWidget* widget = tabWidget->widget(i);
    // Only the contents of the scroll areas are disabled, so the settings of a fixed profile
    // can still be scrolled through.
    const QList<QScrollArea*> areas = widget->findChildren<QScrollArea*>();
    if (areas.isEmpty()) {
      widget->setEnabled(enabled);
      continue;
    }
    for (QScrollArea* area : areas) {
      if (area->widget()) {
        area->widget()->setEnabled(enabled);
      }
    }
  }
}

void DefaultParamsDialog::colorSegmentationToggled(bool) {
  updateColorReductionControls();
}

void DefaultParamsDialog::updateColorReductionControls() {
  // As in the Output step: black and white and mixed pages reduce the colors of the text (with color
  // segmentation), color pages those of the whole page (only posterizing).
  const ColorMode colorMode = currentColorMode();
  const bool textColors = (colorMode == BLACK_AND_WHITE) || (colorMode == MIXED);
  const bool segmentation = textColors && colorSegmentationCB->isChecked();
  const int reduction = textColors ? m_textColorReduction : (m_pagePosterize ? 1 : 0);
  segmenterOptionsWidget->setEnabled(segmentation);
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

void DefaultParamsDialog::colorReductionChanged(const int index) {
  const ColorMode colorMode = currentColorMode();
  if ((colorMode == BLACK_AND_WHITE) || (colorMode == MIXED)) {
    m_textColorReduction = index;
  } else {
    m_pagePosterize = (index == 1);
  }
  updateColorReductionControls();
}

void DefaultParamsDialog::updateAlignmentButtonsEnabled() {
  bool enableHorizontalButtons
      = (hAlignmentModeCB->currentIndex() == 1) ? (matchSizeModeCB->currentIndex() != 0) : false;
  bool enableVerticalButtons = (vAlignmentModeCB->currentIndex() == 1) ? (matchSizeModeCB->currentIndex() != 0) : false;

  alignTopLeftBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignTopBtn->setEnabled(enableVerticalButtons);
  alignTopRightBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignLeftBtn->setEnabled(enableHorizontalButtons);
  alignCenterBtn->setEnabled(enableHorizontalButtons || enableVerticalButtons);
  alignRightBtn->setEnabled(enableHorizontalButtons);
  alignBottomLeftBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
  alignBottomBtn->setEnabled(enableVerticalButtons);
  alignBottomRightBtn->setEnabled(enableHorizontalButtons && enableVerticalButtons);
}

void DefaultParamsDialog::updateAutoModeButtons() {
  bool isAutoHorizontalAlignment = ((hAlignmentModeCB->currentIndex() == 0) || (hAlignmentModeCB->currentIndex() == 2));
  bool isAutoVerticalAlignment = ((vAlignmentModeCB->currentIndex() == 0) || (vAlignmentModeCB->currentIndex() == 2));

  if (isAutoVerticalAlignment && !isAutoHorizontalAlignment) {
    switch (m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal()) {
      case Alignment::LEFT:
        alignLeftBtn->setChecked(true);
        break;
      case Alignment::RIGHT:
        alignRightBtn->setChecked(true);
        break;
      default:
        alignCenterBtn->setChecked(true);
        break;
    }
  } else if (isAutoHorizontalAlignment && !isAutoVerticalAlignment) {
    switch (m_alignmentByButton.at(getCheckedAlignmentButton()).vertical()) {
      case Alignment::TOP:
        alignTopBtn->setChecked(true);
        break;
      case Alignment::BOTTOM:
        alignBottomBtn->setChecked(true);
        break;
      default:
        alignCenterBtn->setChecked(true);
        break;
    }
  }
}

QToolButton* DefaultParamsDialog::getCheckedAlignmentButton() const {
  auto* checkedButton = dynamic_cast<QToolButton*>(m_alignmentButtonGroup->checkedButton());
  if (!checkedButton) {
    checkedButton = alignCenterBtn;
  }
  return checkedButton;
}

void DefaultParamsDialog::despeckleToggled(bool checked) {
  despeckleSlider->setEnabled(checked);
  despeckleValueLabel->setEnabled(checked);
}

void DefaultParamsDialog::despeckleSliderValueChanged(int value) {
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
}

void DefaultParamsDialog::updateAlignmentModeEnabled() {
  const bool isAlignmentNull = (matchSizeModeCB->currentIndex() == 0);

  vAlignmentModeCB->setEnabled(!isAlignmentNull);
  hAlignmentModeCB->setEnabled(!isAlignmentNull);

  updateAlignmentButtonsEnabled();
}

void DefaultParamsDialog::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  rotateLeftBtn->setIcon(iconProvider.getIcon("object-rotate-left"));
  rotateRightBtn->setIcon(iconProvider.getIcon("object-rotate-right"));
  singlePageUncutBtn->setIcon(iconProvider.getIcon("single_page_uncut"));
  pagePlusOffcutBtn->setIcon(iconProvider.getIcon("right_page_plus_offcut"));
  twoPagesBtn->setIcon(iconProvider.getIcon("two_pages"));
  leftFirstBtn->setIcon(iconProvider.getIcon("page_order_left_first"));
  rightFirstBtn->setIcon(iconProvider.getIcon("page_order_right_first"));
  alignTopLeftBtn->setIcon(iconProvider.getIcon("stock-gravity-north-west"));
  alignTopBtn->setIcon(iconProvider.getIcon("stock-gravity-north"));
  alignTopRightBtn->setIcon(iconProvider.getIcon("stock-gravity-north-east"));
  alignRightBtn->setIcon(iconProvider.getIcon("stock-gravity-east"));
  alignBottomRightBtn->setIcon(iconProvider.getIcon("stock-gravity-south-east"));
  alignBottomBtn->setIcon(iconProvider.getIcon("stock-gravity-south"));
  alignBottomLeftBtn->setIcon(iconProvider.getIcon("stock-gravity-south-west"));
  alignLeftBtn->setIcon(iconProvider.getIcon("stock-gravity-west"));
  alignCenterBtn->setIcon(iconProvider.getIcon("stock-center"));
  m_chainIcon = iconProvider.getIcon("stock-vchain");
  m_brokenChainIcon = iconProvider.getIcon("stock-vchain-broken");
}
