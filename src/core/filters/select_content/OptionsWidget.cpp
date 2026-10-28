// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <DefaultParams.h>
#include <UnitsProvider.h>

#include <functional>
#include <utility>

#include "../../Utils.h"
#include "ApplyDialog.h"
#include "DetectionOptions.h"
#include "Settings.h"
#include "Utils.h"

namespace select_content {
OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings,
                             std::shared_ptr<DefaultParams> defaultParams,
                             const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_defaultParams(std::move(defaultParams)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  // Percent fields are as wide as "100 %", their highest value.
  for (QSpinBox* spinBox : {fillFactorSpinBox, borderToleranceSpinBox}) {
    core::Utils::setNumberFieldWidth(spinBox, 100);
  }

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo) {
  auto block = m_connectionManager.getScopedBlock();

  m_pageId = pageInfo.id();
  // The page box is in virtual pixels, which have the equalized resolution.
  m_dpi = Utils::virtualDpi(pageInfo.metadata().dpi());

  contentBoxGroup->setEnabled(false);
  pageBoxGroup->setEnabled(false);

  onUnitsChanged(UnitsProvider::getInstance().getUnits());
}

void OptionsWidget::postUpdateUI(const UiData& uiData) {
  auto block = m_connectionManager.getScopedBlock();

  m_uiData = uiData;

  updateContentModeIndication(uiData.contentDetectionMode());
  updatePageModeIndication(uiData.pageDetectionMode());

  contentBoxGroup->setEnabled(true);
  pageBoxGroup->setEnabled(true);

  updateContentDetectOptionsDisplay();
  updatePageDetectOptionsDisplay();
  updatePageRectSize(m_uiData.pageRect().size());
}

void OptionsWidget::manualContentRectSet(const QRectF& contentRect) {
  m_uiData.setContentRect(contentRect);
  m_uiData.setContentDetectionMode(MODE_MANUAL);
  {
    // The mode is set here already; checking the button mustn't set it again.
    auto block = m_connectionManager.getScopedBlock();
    updateContentModeIndication(MODE_MANUAL);
  }
  updateContentDetectOptionsDisplay();

  commitCurrentParams();

  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::manualPageRectSet(const QRectF& pageRect) {
  m_uiData.setPageRect(pageRect);
  m_uiData.setPageDetectionMode(MODE_MANUAL);
  {
    auto block = m_connectionManager.getScopedBlock();
    updatePageModeIndication(MODE_MANUAL);
  }
  updatePageDetectOptionsDisplay();
  updatePageRectSize(pageRect.size());

  commitCurrentParams();

  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::updatePageRectSize(const QSizeF& size) {
  auto block = m_connectionManager.getScopedBlock();

  double width = size.width();
  double height = size.height();
  UnitsProvider::getInstance().convertFrom(width, height, PIXELS, m_dpi);

  widthSpinBox->setValue(width);
  heightSpinBox->setValue(height);
}

void OptionsWidget::contentDetectToggled(const AutoManualMode mode) {
  m_uiData.setContentDetectionMode(mode);
  updateContentDetectOptionsDisplay();
  commitCurrentParams();

  if (mode != MODE_MANUAL) {
    emit reloadRequested();
  }
}

void OptionsWidget::pageDetectToggled(const AutoManualMode mode) {
  const bool needUpdateState = ((mode == MODE_MANUAL) && (m_uiData.pageDetectionMode() == MODE_DISABLED));

  m_uiData.setPageDetectionMode(mode);
  updatePageDetectOptionsDisplay();
  commitCurrentParams();

  if (mode != MODE_MANUAL) {
    emit reloadRequested();
  } else if (needUpdateState) {
    emit pageRectStateChanged(true);
    emit invalidateThumbnail(m_pageId);
  }
}

void OptionsWidget::fineTuningChanged(bool checked) {
  m_uiData.setFineTuneCornersEnabled(checked);
  commitCurrentParams();
  if (m_uiData.pageDetectionMode() == MODE_AUTO) {
    emit reloadRequested();
  }
}

void OptionsWidget::detectionOptionsChanged() {
  // Settings of the page: other pages get them with "Apply to ..." (Content box).
  m_uiData.setDetectionOptions(
      DetectionOptions(fillFactorSpinBox->value(), borderToleranceSpinBox->value(), ignorePageBoxCB->isChecked()));
  if (m_uiData.contentDetectionMode() != MODE_AUTO) {
    // Kept for when the page is switched to Auto.
    commitCurrentParams();
  } else {
    // The task compares the new settings with those the box was detected with.
    Dependencies deps = m_uiData.dependencies();
    deps.assumeDefaultDetectionOptionsIfUnknown();
    m_uiData.setDependencies(deps);
    commitCurrentParams();
    emit reloadRequested();
  }
}

void OptionsWidget::updateContentModeIndication(const AutoManualMode mode) {
  switch (mode) {
    case MODE_AUTO:
      contentDetectAutoBtn->setChecked(true);
      break;
    case MODE_MANUAL:
      contentDetectManualBtn->setChecked(true);
      break;
    case MODE_DISABLED:
      contentDetectDisableBtn->setChecked(true);
      break;
  }
}

void OptionsWidget::updatePageModeIndication(const AutoManualMode mode) {
  switch (mode) {
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
}

void OptionsWidget::updatePageDetectOptionsDisplay() {
  fineTuneBtn->setChecked(m_uiData.isFineTuningCornersEnabled());
  // Always shown, grayed out where the page box mode doesn't use them.
  fineTuneBtn->setEnabled(m_uiData.pageDetectionMode() == MODE_AUTO);
  dimensionsWidget->setEnabled(m_uiData.pageDetectionMode() == MODE_MANUAL);
}

void OptionsWidget::updateContentDetectOptionsDisplay() {
  auto block = m_connectionManager.getScopedBlock();

  const DetectionOptions options
      = m_uiData.detectionOptions().value_or(m_defaultParams->getSelectContentParams().getDetectionOptions());
  fillFactorSpinBox->setValue(options.fillFactor());
  borderToleranceSpinBox->setValue(options.borderTolerance());
  ignorePageBoxCB->setChecked(options.ignorePageBox());
  // Always shown, grayed out where the content box mode doesn't use them.
  contentDetectOptions->setEnabled(m_uiData.contentDetectionMode() == MODE_AUTO);
}

void OptionsWidget::dimensionsChangedLocally(double /*value*/) {
  double widthSpinBoxValue = widthSpinBox->value();
  double heightSpinBoxValue = heightSpinBox->value();
  UnitsProvider::getInstance().convertTo(widthSpinBoxValue, heightSpinBoxValue, PIXELS, m_dpi);

  QRectF newPageRect = m_uiData.pageRect();
  newPageRect.setSize(QSizeF(widthSpinBoxValue, heightSpinBoxValue));

  emit pageRectChangedLocally(newPageRect);
}

void OptionsWidget::commitCurrentParams() {
  updateDependenciesIfNecessary();

  Params params(m_uiData.contentRect(), m_uiData.contentSizeMM(), m_uiData.pageRect(), m_uiData.dependencies(),
                m_uiData.contentDetectionMode(), m_uiData.pageDetectionMode(), m_uiData.isFineTuningCornersEnabled());
  params.setDetectionOptions(m_uiData.detectionOptions());
  m_settings->setPageParams(m_pageId, params);
}

void OptionsWidget::updateDependenciesIfNecessary() {
  // On switching to manual mode the page dependencies isn't updated
  // as Task::process isn't called, so we need to update it manually.
  if (!(m_uiData.contentDetectionMode() == MODE_MANUAL || m_uiData.pageDetectionMode() == MODE_MANUAL)) {
    return;
  }

  Dependencies deps = m_uiData.dependencies();
  if (m_uiData.contentDetectionMode() == MODE_MANUAL) {
    deps.setContentDetectionMode(MODE_MANUAL);
  }
  if (m_uiData.pageDetectionMode() == MODE_MANUAL) {
    deps.setPageDetectionMode(MODE_MANUAL);
  }
  m_uiData.setDependencies(deps);
}

void OptionsWidget::showApplyToDialog() {
  auto* dialog = new ApplyDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &ApplyDialog::applySelection, this, &OptionsWidget::applySelection);
  dialog->show();
}

void OptionsWidget::applySelection(const std::set<PageId>& pages, const bool applyContentBox, const bool applyPageBox) {
  if (pages.empty()) {
    return;
  }

  // The empty dependencies make the task detect both boxes of each target page again.
  Params params(m_uiData.contentRect(), m_uiData.contentSizeMM(), m_uiData.pageRect(), Dependencies(),
                m_uiData.contentDetectionMode(), m_uiData.pageDetectionMode(), m_uiData.isFineTuningCornersEnabled());
  // The detection settings go with the content box, as shown, also if the page has none of its own yet.
  params.setDetectionOptions(
      m_uiData.detectionOptions().value_or(m_defaultParams->getSelectContentParams().getDetectionOptions()));

  for (const PageId& pageId : pages) {
    if (m_pageId == pageId) {
      continue;
    }

    Params newParams(params);
    std::unique_ptr<Params> oldParams = m_settings->getPageParams(pageId);
    // The box that isn't applied keeps its mode on the target page; a page without params
    // keeps the defaults.
    const Params keptParams = oldParams ? *oldParams : Params(Dependencies());
    if (!applyPageBox) {
      newParams.setPageDetectionMode(keptParams.pageDetectionMode());
      newParams.setPageRect(keptParams.pageRect());
      newParams.setFineTuneCornersEnabled(keptParams.isFineTuningEnabled());
    }
    if (!applyContentBox) {
      newParams.setContentDetectionMode(keptParams.contentDetectionMode());
      newParams.setContentRect(keptParams.contentRect());
      newParams.setContentSizeMM(keptParams.contentSizeMM());
      newParams.setDetectionOptions(keptParams.detectionOptions());
    }
    // REVIEW-NOTE (2026-10): manual page and content boxes are only shifted for target pages that already
    // have params -> unprocessed pages get the boxes in the source page's coordinates. Not fixed because the
    // shift needs the target outline, which is only known in Task.
    if (oldParams) {
      if (applyPageBox && (newParams.pageDetectionMode() == MODE_MANUAL)) {
        QRectF correctedPageRect = newParams.pageRect();
        // newParams carries empty dependencies by design, so the source image has to be
        // taken from the page we are copying from.
        const QRectF sourceImageRect = m_uiData.dependencies().rotatedPageOutline().boundingRect();
        const QRectF currentImageRect = oldParams->dependencies().rotatedPageOutline().boundingRect();
        if (sourceImageRect.isValid() && currentImageRect.isValid()) {
          correctedPageRect.translate((currentImageRect.width() - sourceImageRect.width()) / 2,
                                      (currentImageRect.height() - sourceImageRect.height()) / 2);
          newParams.setPageRect(correctedPageRect);
        }
      }
      if (applyContentBox && (newParams.contentDetectionMode() == MODE_MANUAL) && !newParams.contentRect().isEmpty()) {
        QRectF correctedContentRect = newParams.contentRect();
        const QRectF& sourcePageRect = m_uiData.pageRect();
        const QRectF& newPageRect = newParams.pageRect();
        correctedContentRect.translate(newPageRect.x() - sourcePageRect.x(), newPageRect.y() - sourcePageRect.y());
        newParams.setContentRect(correctedContentRect);
      }
    }

    m_settings->setPageParams(pageId, newParams);
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  emit reloadRequested();
}  // OptionsWidget::applySelection


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

  widthSpinBox->setDecimals(decimals);
  widthSpinBox->setSingleStep(step);
  heightSpinBox->setDecimals(decimals);
  heightSpinBox->setSingleStep(step);
  // The unit is shown in the fields.
  const QString unitSuffix = QChar(' ') + unitsToLocalizedString(units);
  widthSpinBox->setSuffix(unitSuffix);
  heightSpinBox->setSuffix(unitSuffix);
  // As the size fields in the step "Margins": three digits before the decimal point, in pixels four.
  const double widestSize = (units == PIXELS) ? 9999.9 : (decimals == 1) ? 999.9 : 99.99;
  for (QDoubleSpinBox* spinBox : {widthSpinBox, heightSpinBox}) {
    core::Utils::setNumberFieldWidth(spinBox, widestSize);
  }

  updatePageRectSize(m_uiData.pageRect().size());
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidget::setupUiConnections() {
  CONNECT(widthSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::dimensionsChangedLocally);
  CONNECT(heightSpinBox, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::dimensionsChangedLocally);
  // Only the button that gets checked acts. Not on "pressed": the user may still move off the button.
  const auto connectMode
      = [this](QAbstractButton* button, void (OptionsWidget::*handler)(AutoManualMode), const AutoManualMode mode) {
          CONNECT(button, &QAbstractButton::toggled, this, [this, handler, mode](const bool checked) {
            if (checked) {
              (this->*handler)(mode);
            }
          });
        };
  connectMode(contentDetectAutoBtn, &OptionsWidget::contentDetectToggled, MODE_AUTO);
  connectMode(contentDetectManualBtn, &OptionsWidget::contentDetectToggled, MODE_MANUAL);
  connectMode(contentDetectDisableBtn, &OptionsWidget::contentDetectToggled, MODE_DISABLED);
  connectMode(pageDetectAutoBtn, &OptionsWidget::pageDetectToggled, MODE_AUTO);
  connectMode(pageDetectManualBtn, &OptionsWidget::pageDetectToggled, MODE_MANUAL);
  connectMode(pageDetectDisableBtn, &OptionsWidget::pageDetectToggled, MODE_DISABLED);
  CONNECT(fineTuneBtn, &QAbstractButton::toggled, this, &OptionsWidget::fineTuningChanged);
  CONNECT(fillFactorSpinBox, &QSpinBox::valueChanged, this, &OptionsWidget::detectionOptionsChanged);
  CONNECT(borderToleranceSpinBox, &QSpinBox::valueChanged, this, &OptionsWidget::detectionOptionsChanged);
  CONNECT(ignorePageBoxCB, &QAbstractButton::toggled, this, &OptionsWidget::detectionOptionsChanged);
  CONNECT(applyToBtn, &QAbstractButton::clicked, this, &OptionsWidget::showApplyToDialog);
}

#undef CONNECT


/*========================= OptionsWidget::UiData ======================*/

OptionsWidget::UiData::UiData()
    : m_contentDetectionMode(MODE_AUTO), m_pageDetectionMode(MODE_DISABLED), m_fineTuneCornersEnabled(false) {}

OptionsWidget::UiData::~UiData() = default;
}  // namespace select_content
