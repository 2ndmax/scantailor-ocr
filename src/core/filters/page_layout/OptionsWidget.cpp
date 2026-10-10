// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <ColorSchemeManager.h>
#include <UnitsProvider.h>
#include <core/IconProvider.h>

#include <QButtonGroup>
#include <QLineEdit>
#include <QSettings>
#include <cmath>
#include <utility>

#include "../../Utils.h"
#include "ApplyDialog.h"
#include "ApplyMarginsDialog.h"
#include "ProjectPages.h"
#include "Settings.h"

using namespace core;

namespace page_layout {
OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings,
                             std::shared_ptr<ProjectPages> pages,
                             const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pages(std::move(pages)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_sourceDpiFocusWidget(nullptr),
      m_leftRightLinked(true),
      m_topBottomLinked(true),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  {
    QSettings appSettings;
    m_leftRightLinked = appSettings.value("margins/leftRightLinked", true).toBool();
    m_topBottomLinked = appSettings.value("margins/topBottomLinked", true).toBool();
  }

  setupUi(this);
  setupIcons();
  setupSourceDpiControls();

  updateLinkDisplay(topBottomLink, m_topBottomLinked);
  updateLinkDisplay(leftRightLink, m_leftRightLinked);
  updateAlignmentButtonsEnabled();

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

  setupUiConnections();

  // The common size can change whenever a page is redrawn with new settings.
  connect(this, qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail), this,
          &OptionsWidget::updateCommonSizeDisplay);
  connect(this, &OptionsWidget::invalidateAllThumbnails, this, &OptionsWidget::updateCommonSizeDisplay);
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo, const Margins& marginsMm, const Alignment& alignment) {
  commitSourceDpiIfValid();
  m_sourceDpiFocusWidget = isSourceDpiFieldFocused() ? focusWidget() : nullptr;

  auto block = m_connectionManager.getScopedBlock();

  m_pageId = pageInfo.id();
  m_dpi = pageInfo.metadata().dpi();
  m_sourceImagePixelSize = pageInfo.metadata().size();
  m_marginsMM = marginsMm;
  m_alignment = alignment;

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

  alignWithOthersCB->setChecked(!alignment.isNull());
  freezeAggregateHardSizeCb->setChecked(m_settings->isAggregateHardSizeFrozen());

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

  updateAlignmentModeEnabled();
  updateAutoModeButtons();

  autoMargins->setChecked(m_settings->isPageAutoMarginsEnabled(m_pageId));
  updateMarginsControlsEnabled();

  m_leftRightLinked = m_leftRightLinked && (marginsMm.left() == marginsMm.right());
  m_topBottomLinked = m_topBottomLinked && (marginsMm.top() == marginsMm.bottom());
  updateLinkDisplay(topBottomLink, m_topBottomLinked);
  updateLinkDisplay(leftRightLink, m_leftRightLinked);

  // The source resolution stays editable while the page is reloaded.
  marginsGroup->setEnabled(false);
  alignmentGroup->setEnabled(false);

  onUnitsChanged(UnitsProvider::getInstance().getUnits());
}  // OptionsWidget::preUpdateUI

void OptionsWidget::postUpdateUI() {
  auto block = m_connectionManager.getScopedBlock();

  marginsGroup->setEnabled(true);
  alignmentGroup->setEnabled(true);

  m_marginsMM = m_settings->getHardMarginsMM(m_pageId);
  updateMarginsDisplay();
  updateSourceDpiDisplay();
  updateCommonSizeDisplay();

  if (m_sourceDpiFocusWidget) {
    m_sourceDpiFocusWidget->setFocus(Qt::OtherFocusReason);
    m_sourceDpiFocusWidget = nullptr;
  }
}

void OptionsWidget::marginsSetExternally(const Margins& marginsMm) {
  m_marginsMM = marginsMm;

  if (autoMargins->isChecked()) {
    autoMargins->setChecked(false);
    m_settings->setPageAutoMarginsEnabled(m_pageId, false);
    updateMarginsControlsEnabled();
  }

  updateMarginsDisplay();
}

void OptionsWidget::onUnitsChanged(Units units) {
  auto block = m_connectionManager.getScopedBlock();

  int decimals;
  double step;
  switch (units) {
    case PIXELS:
    case MILLIMETRES:
      decimals = 1;
      step = 1.0;
      break;
    default:
      decimals = 2;
      step = 0.01;
      break;
  }

  topMarginSpinBox->setDecimals(decimals);
  topMarginSpinBox->setSingleStep(step);
  bottomMarginSpinBox->setDecimals(decimals);
  bottomMarginSpinBox->setSingleStep(step);
  leftMarginSpinBox->setDecimals(decimals);
  leftMarginSpinBox->setSingleStep(step);
  rightMarginSpinBox->setDecimals(decimals);
  rightMarginSpinBox->setSingleStep(step);
  // The unit is shown in the fields.
  const QString unitSuffix = QChar(' ') + unitsToLocalizedString(units);
  for (QDoubleSpinBox* spinBox : {topMarginSpinBox, bottomMarginSpinBox, leftMarginSpinBox, rightMarginSpinBox}) {
    spinBox->setSuffix(unitSuffix);
  }

  updateMarginsDisplay();
  updateCommonSizeDisplay();
}

void OptionsWidget::horMarginsChanged(const double val) {
  if (m_leftRightLinked) {
    auto block = m_connectionManager.getScopedBlock();
    leftMarginSpinBox->setValue(val);
    rightMarginSpinBox->setValue(val);
  }

  double dummy;
  double leftMarginSpinBoxValue = leftMarginSpinBox->value();
  double rightMarginSpinBoxValue = rightMarginSpinBox->value();
  UnitsProvider::getInstance().convertTo(leftMarginSpinBoxValue, dummy, MILLIMETRES, m_dpi);
  UnitsProvider::getInstance().convertTo(rightMarginSpinBoxValue, dummy, MILLIMETRES, m_dpi);

  m_marginsMM.setLeft(leftMarginSpinBoxValue);
  m_marginsMM.setRight(rightMarginSpinBoxValue);

  emit marginsSetLocally(m_marginsMM);
}

void OptionsWidget::vertMarginsChanged(const double val) {
  if (m_topBottomLinked) {
    auto block = m_connectionManager.getScopedBlock();
    topMarginSpinBox->setValue(val);
    bottomMarginSpinBox->setValue(val);
  }

  double dummy;
  double topMarginSpinBoxValue = topMarginSpinBox->value();
  double bottomMarginSpinBoxValue = bottomMarginSpinBox->value();
  UnitsProvider::getInstance().convertTo(dummy, topMarginSpinBoxValue, MILLIMETRES, m_dpi);
  UnitsProvider::getInstance().convertTo(dummy, bottomMarginSpinBoxValue, MILLIMETRES, m_dpi);

  m_marginsMM.setTop(topMarginSpinBoxValue);
  m_marginsMM.setBottom(bottomMarginSpinBoxValue);

  emit marginsSetLocally(m_marginsMM);
}

void OptionsWidget::topBottomLinkClicked() {
  m_topBottomLinked = !m_topBottomLinked;
  QSettings().setValue("margins/topBottomLinked", m_topBottomLinked);
  updateLinkDisplay(topBottomLink, m_topBottomLinked);
  topBottomLinkToggled(m_topBottomLinked);
}

void OptionsWidget::leftRightLinkClicked() {
  m_leftRightLinked = !m_leftRightLinked;
  QSettings().setValue("margins/leftRightLinked", m_leftRightLinked);
  updateLinkDisplay(leftRightLink, m_leftRightLinked);
  leftRightLinkToggled(m_leftRightLinked);
}

void OptionsWidget::alignWithOthersToggled() {
  m_alignment.setNull(!alignWithOthersCB->isChecked());

  updateAlignmentModeEnabled();
  emit alignmentChanged(m_alignment);
}

void OptionsWidget::autoMarginsToggled(bool checked) {
  m_settings->setPageAutoMarginsEnabled(m_pageId, checked);
  updateMarginsControlsEnabled();

  emit reloadRequested();
}

void OptionsWidget::horizontalAlignmentModeChanged(int idx) {
  switch (idx) {
    case 0:
      m_alignment.setHorizontal(Alignment::HAUTO);
      updateAutoModeButtons();
      break;
    case 1:
      m_alignment.setHorizontal(m_alignmentByButton.at(getCheckedAlignmentButton()).horizontal());
      break;
    case 2:
      m_alignment.setHorizontal(Alignment::HORIGINAL);
      updateAutoModeButtons();
      break;
    default:
      break;
  }

  updateAlignmentButtonsEnabled();
  emit alignmentChanged(m_alignment);
}

void OptionsWidget::verticalAlignmentModeChanged(int idx) {
  switch (idx) {
    case 0:
      m_alignment.setVertical(Alignment::VAUTO);
      updateAutoModeButtons();
      break;
    case 1:
      m_alignment.setVertical(m_alignmentByButton.at(getCheckedAlignmentButton()).vertical());
      break;
    case 2:
      m_alignment.setVertical(Alignment::VORIGINAL);
      updateAutoModeButtons();
      break;
    default:
      break;
  }

  updateAlignmentButtonsEnabled();
  emit alignmentChanged(m_alignment);
}

void OptionsWidget::alignmentButtonClicked() {
  auto* const button = dynamic_cast<QToolButton*>(sender());
  assert(button);

  const Alignment& alignment = m_alignmentByButton.at(button);

  if (m_alignment.isAutoVertical()) {
    m_alignment.setHorizontal(alignment.horizontal());
  } else if (m_alignment.isAutoHorizontal()) {
    m_alignment.setVertical(alignment.vertical());
  } else {
    m_alignment = alignment;
  }

  emit alignmentChanged(m_alignment);
}

void OptionsWidget::showApplyMarginsDialog() {
  auto* dialog = new ApplyMarginsDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Margins"));
  connect(dialog, &ApplyMarginsDialog::accepted, this, &OptionsWidget::applyMargins);
  dialog->show();
}

void OptionsWidget::showApplyAlignmentDialog() {
  auto* dialog = new ApplyDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setWindowTitle(tr("Apply Alignment"));
  connect(dialog, &ApplyDialog::accepted, this, &OptionsWidget::applyAlignment);
  dialog->show();
}

void OptionsWidget::applyMargins(const std::set<PageId>& pages,
                                 bool applyLeft,
                                 bool applyRight,
                                 bool applyTop,
                                 bool applyBottom) {
  if (pages.empty()) {
    return;
  }

  const bool autoMarginsEnabled = m_settings->isPageAutoMarginsEnabled(m_pageId);
  for (const PageId& pageId : pages) {
    m_settings->setPageAutoMarginsEnabled(pageId, autoMarginsEnabled);
    if (autoMarginsEnabled) {
      m_settings->invalidateContentSize(pageId);
    } else {
      Margins target = m_settings->getHardMarginsMM(pageId);
      if (applyLeft) {
        target.setLeft(m_marginsMM.left());
      }
      if (applyRight) {
        target.setRight(m_marginsMM.right());
      }
      if (applyTop) {
        target.setTop(m_marginsMM.top());
      }
      if (applyBottom) {
        target.setBottom(m_marginsMM.bottom());
      }
      m_settings->setHardMarginsMM(pageId, target);
    }
  }

  emit aggregateHardSizeChanged();
  emit invalidateAllThumbnails();
}

void OptionsWidget::applyAlignment(const std::set<PageId>& pages) {
  if (pages.empty()) {
    return;
  }

  for (const PageId& pageId : pages) {
    if (pageId == m_pageId) {
      continue;
    }

    m_settings->setPageAlignment(pageId, m_alignment);
  }

  emit invalidateAllThumbnails();
}

void OptionsWidget::freezeAggregateHardSizeToggled(const bool checked) {
  m_settings->setAggregateHardSizeFrozen(checked);
  emit aggregateHardSizeChanged();
  // Released, the size may differ from the one the other pages were drawn with.
  emit invalidateAllThumbnails();
}

void OptionsWidget::updateCommonSizeDisplay() {
  const QSizeF sizeMm(m_settings->getAggregateHardSizeMM());
  if (sizeMm.isEmpty() || m_dpi.isNull()) {
    commonSizeValue->setText(tr("no matched pages"));
    return;
  }

  double width = sizeMm.width();
  double height = sizeMm.height();
  UnitsProvider::getInstance().convertFrom(width, height, MILLIMETRES, m_dpi);

  // Rounded like the image size in the status bar.
  const Units units = UnitsProvider::getInstance().getUnits();
  switch (units) {
    case PIXELS:
    case MILLIMETRES:
      width = std::round(width);
      height = std::round(height);
      break;
    default:
      width = std::round(width * 10) / 10;
      height = std::round(height * 10) / 10;
      break;
  }
  commonSizeValue->setText(QString("%1 x %2 %3").arg(width).arg(height).arg(unitsToLocalizedString(units)));
}

void OptionsWidget::updateMarginsDisplay() {
  auto block = m_connectionManager.getScopedBlock();

  double topMarginValue = m_marginsMM.top();
  double bottomMarginValue = m_marginsMM.bottom();
  double leftMarginValue = m_marginsMM.left();
  double rightMarginValue = m_marginsMM.right();
  UnitsProvider::getInstance().convertFrom(leftMarginValue, topMarginValue, MILLIMETRES, m_dpi);
  UnitsProvider::getInstance().convertFrom(rightMarginValue, bottomMarginValue, MILLIMETRES, m_dpi);

  topMarginSpinBox->setValue(topMarginValue);
  bottomMarginSpinBox->setValue(bottomMarginValue);
  leftMarginSpinBox->setValue(leftMarginValue);
  rightMarginSpinBox->setValue(rightMarginValue);

  m_leftRightLinked = m_leftRightLinked && (leftMarginSpinBox->value() == rightMarginSpinBox->value());
  m_topBottomLinked = m_topBottomLinked && (topMarginSpinBox->value() == bottomMarginSpinBox->value());
  updateLinkDisplay(topBottomLink, m_topBottomLinked);
  updateLinkDisplay(leftRightLink, m_leftRightLinked);
}

void OptionsWidget::updateLinkDisplay(QToolButton* button, const bool linked) {
  button->setIcon(linked ? m_chainIcon : m_brokenChainIcon);
}

void OptionsWidget::updateAlignmentButtonsEnabled() {
  bool enableHorizontalButtons = !m_alignment.isAutoHorizontal() ? alignWithOthersCB->isChecked() : false;
  bool enableVerticalButtons = !m_alignment.isAutoVertical() ? alignWithOthersCB->isChecked() : false;

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

void OptionsWidget::updateMarginsControlsEnabled() {
  const bool enabled = !m_settings->isPageAutoMarginsEnabled(m_pageId);

  topMarginSpinBox->setEnabled(enabled);
  bottomMarginSpinBox->setEnabled(enabled);
  leftMarginSpinBox->setEnabled(enabled);
  rightMarginSpinBox->setEnabled(enabled);
  topBottomLink->setEnabled(enabled);
  leftRightLink->setEnabled(enabled);
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidget::setupUiConnections() {
  CONNECT(topMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::vertMarginsChanged);
  CONNECT(bottomMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::vertMarginsChanged);
  CONNECT(leftMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::horMarginsChanged);
  CONNECT(rightMarginSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::horMarginsChanged);
  CONNECT(autoMargins, &QAbstractButton::toggled, this, &OptionsWidget::autoMarginsToggled);
  CONNECT(hAlignmentModeCB, &QComboBox::currentIndexChanged, this, &OptionsWidget::horizontalAlignmentModeChanged);
  CONNECT(vAlignmentModeCB, &QComboBox::currentIndexChanged, this, &OptionsWidget::verticalAlignmentModeChanged);
  CONNECT(topBottomLink, &QAbstractButton::clicked, this, &OptionsWidget::topBottomLinkClicked);
  CONNECT(leftRightLink, &QAbstractButton::clicked, this, &OptionsWidget::leftRightLinkClicked);
  CONNECT(applyMarginsBtn, &QAbstractButton::clicked, this, &OptionsWidget::showApplyMarginsDialog);
  CONNECT(fixDpiBtn, &QAbstractButton::clicked, this, &OptionsWidget::onFixDpiClicked);
  CONNECT(sourceXDpi, &QComboBox::activated, this, &OptionsWidget::sourceDpiActivated);
  CONNECT(sourceYDpi, &QComboBox::activated, this, &OptionsWidget::sourceDpiActivated);
  CONNECT(sourceXDpi->lineEdit(), &QLineEdit::editingFinished, this, &OptionsWidget::sourceDpiEditingFinished);
  CONNECT(sourceYDpi->lineEdit(), &QLineEdit::editingFinished, this, &OptionsWidget::sourceDpiEditingFinished);
  CONNECT(alignWithOthersCB, &QAbstractButton::toggled, this, &OptionsWidget::alignWithOthersToggled);
  CONNECT(applyAlignmentBtn, &QAbstractButton::clicked, this, &OptionsWidget::showApplyAlignmentDialog);
  CONNECT(freezeAggregateHardSizeCb, &QAbstractButton::clicked, this, &OptionsWidget::freezeAggregateHardSizeToggled);
  for (const auto& kv : m_alignmentByButton) {
    CONNECT(kv.first, &QAbstractButton::clicked, this, &OptionsWidget::alignmentButtonClicked);
  }
}

#undef CONNECT

bool OptionsWidget::leftRightLinked() const {
  return m_leftRightLinked;
}

bool OptionsWidget::topBottomLinked() const {
  return m_topBottomLinked;
}

const Margins& OptionsWidget::marginsMM() const {
  return m_marginsMM;
}

const Alignment& OptionsWidget::alignment() const {
  return m_alignment;
}

void OptionsWidget::updateAutoModeButtons() {
  auto block = m_connectionManager.getScopedBlock();

  if (m_alignment.isAutoVertical() && !m_alignment.isAutoHorizontal()) {
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
  } else if (m_alignment.isAutoHorizontal() && !m_alignment.isAutoVertical()) {
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

QToolButton* OptionsWidget::getCheckedAlignmentButton() const {
  auto* checkedButton = dynamic_cast<QToolButton*>(m_alignmentButtonGroup->checkedButton());
  if (!checkedButton) {
    checkedButton = alignCenterBtn;
  }
  return checkedButton;
}

void OptionsWidget::updateAlignmentModeEnabled() {
  const bool isAlignmentNull = m_alignment.isNull();

  vAlignmentModeCB->setEnabled(!isAlignmentNull);
  hAlignmentModeCB->setEnabled(!isAlignmentNull);

  updateAlignmentButtonsEnabled();
}

void OptionsWidget::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  topBottomLink->setIcon(iconProvider.getIcon("ver_chain"));
  leftRightLink->setIcon(iconProvider.getIcon("ver_chain"));
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

void OptionsWidget::onFixDpiClicked() {
  emit fixDpiRequested();
}

void OptionsWidget::setupSourceDpiControls() {
  // Lists of the usual values that also take any other value typed in.
  for (QComboBox* field : {sourceXDpi, sourceYDpi}) {
    for (const int dpi : {300, 400, 600, 1200}) {
      field->addItem(core::Utils::dpiText(dpi));
    }
    field->setInsertPolicy(QComboBox::NoInsert);
    field->setValidator(core::Utils::createDpiValidator(field));
    core::Utils::setDpiFieldWidth(field);
  }

  m_sourceDpiNormalPalette = sourceXDpi->lineEdit()->palette();
  m_sourceDpiErrorPalette = m_sourceDpiNormalPalette;
  const QColor errorColor(ColorSchemeManager::instance().getColorParam("FixDpiDialogErrorText", QColor(Qt::red)));
  m_sourceDpiErrorPalette.setColor(QPalette::Text, errorColor);
}

void OptionsWidget::updateSourceDpiDisplay() {
  auto block = m_connectionManager.getScopedBlock();

  if (m_dpi.isNull()) {
    sourceXDpi->setEditText(QString());
    sourceYDpi->setEditText(QString());
  } else {
    sourceXDpi->setEditText(core::Utils::dpiText(m_dpi.horizontal()));
    sourceYDpi->setEditText(core::Utils::dpiText(m_dpi.vertical()));
  }

  const ImageMetadata metadata(m_sourceImagePixelSize, m_dpi);
  decorateSourceDpiField(sourceXDpi, metadata.horizontalDpiStatus());
  decorateSourceDpiField(sourceYDpi, metadata.verticalDpiStatus());
}

void OptionsWidget::commitSourceDpiIfValid() {
  if (m_pageId.isNull() || !m_pages) {
    return;
  }

  bool xOk = false;
  bool yOk = false;
  const int horizontalDpi = core::Utils::dpiFromText(sourceXDpi->currentText(), &xOk);
  const int verticalDpi = core::Utils::dpiFromText(sourceYDpi->currentText(), &yOk);
  if (!xOk || !yOk) {
    return;
  }

  const Dpi dpi(horizontalDpi, verticalDpi);
  if (dpi == m_dpi) {
    return;
  }

  const ImageMetadata updated(m_sourceImagePixelSize, dpi);
  if (!updated.isDpiOK()) {
    return;
  }

  m_pages->updateImageMetadata(m_pageId.imageId(), updated);
  m_dpi = dpi;
  // Updates the page list and reloads the page.
  emit sourceDpiChanged();
  updateMarginsDisplay();
}

void OptionsWidget::decorateSourceDpiField(QComboBox* field, const ImageMetadata::DpiStatus dpiStatus) {
  if (dpiStatus == ImageMetadata::DPI_OK) {
    field->lineEdit()->setPalette(m_sourceDpiNormalPalette);
    field->setToolTip(sourceDpiLabel->toolTip());
    return;
  }

  field->lineEdit()->setPalette(m_sourceDpiErrorPalette);
  switch (dpiStatus) {
    case ImageMetadata::DPI_TOO_LARGE:
      field->setToolTip(tr("DPI is too large and most likely wrong."));
      break;
    case ImageMetadata::DPI_TOO_SMALL:
      field->setToolTip(
          tr("DPI is too small. Even if it's correct, you are not going to get acceptable results with it."));
      break;
    case ImageMetadata::DPI_TOO_SMALL_FOR_THIS_PIXEL_SIZE:
      field->setToolTip(
          tr("An extremely low DPI value. That might correspond to a very large paper size for the pixel size in "
             "question."));
      break;
    default:
      field->setToolTip(sourceDpiLabel->toolTip());
      break;
  }
}

bool OptionsWidget::isSourceDpiFieldFocused() const {
  const QWidget* const focused = focusWidget();
  return (focused == sourceXDpi) || (focused == sourceYDpi) || (focused == sourceXDpi->lineEdit())
         || (focused == sourceYDpi->lineEdit());
}

void OptionsWidget::sourceDpiActivated() {
  auto* chosen = qobject_cast<QComboBox*>(sender());
  if (!chosen) {
    return;
  }
  // Both values are almost always the same, so a value chosen from the list sets both while
  // they are.  Different values stay different; typing changes only the one field.
  QComboBox* other = (chosen == sourceXDpi) ? sourceYDpi : sourceXDpi;
  if (m_dpi.isNull() || (m_dpi.horizontal() == m_dpi.vertical())) {
    other->setEditText(chosen->currentText());
  }
  sourceDpiEditingFinished();
}

void OptionsWidget::sourceDpiEditingFinished() {
  bool xOk = false;
  bool yOk = false;
  const int horizontalDpi = core::Utils::dpiFromText(sourceXDpi->currentText(), &xOk);
  const int verticalDpi = core::Utils::dpiFromText(sourceYDpi->currentText(), &yOk);
  if (xOk && yOk) {
    // Typed without the unit, the value is shown like the others.
    sourceXDpi->setEditText(core::Utils::dpiText(horizontalDpi));
    sourceYDpi->setEditText(core::Utils::dpiText(verticalDpi));
    const ImageMetadata metadata(m_sourceImagePixelSize, Dpi(horizontalDpi, verticalDpi));
    decorateSourceDpiField(sourceXDpi, metadata.horizontalDpiStatus());
    decorateSourceDpiField(sourceYDpi, metadata.verticalDpiStatus());
  }

  commitSourceDpiIfValid();
}
}  // namespace page_layout
