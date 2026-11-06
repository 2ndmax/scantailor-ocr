// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <core/IconProvider.h>

#include <QSize>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <utility>

#include "../../Utils.h"
#include "ApplyDialog.h"
#include "Filter.h"
#include "ProjectPages.h"
#include "Settings.h"
#include "UnitsConverter.h"
#include "UnitsProvider.h"

namespace fix_orientation {

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  onUnitsChanged(UnitsProvider::getInstance().getUnits());
  setupIcons();

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo, const OrthogonalRotation rotation) {
  auto block = m_connectionManager.getScopedBlock();

  m_pageId = pageInfo.id();
  m_imagePixelSize = pageInfo.metadata().size();
  m_imageDpi = pageInfo.metadata().dpi();
  m_rotation = rotation;
  setRotationPixmap();
  // An image without a resolution shows pixels, so the format may change with the page.
  onUnitsChanged(UnitsProvider::getInstance().getUnits());
}

void OptionsWidget::onUnitsChanged(const Units /*units*/) {
  auto block = m_connectionManager.getScopedBlock();

  const Units units = fieldUnits();
  int decimals;
  double step;
  double widestValue;
  switch (units) {
    case PIXELS:
      decimals = 0;
      step = 1.0;
      widestValue = 9999;
      break;
    case MILLIMETRES:
      decimals = 1;
      step = 1.0;
      widestValue = 999.9;
      break;
    default:
      decimals = 2;
      step = 0.01;
      widestValue = 99.99;
      break;
  }
  const QString unitSuffix = QChar(' ') + unitsToLocalizedString(units);
  for (QDoubleSpinBox* spinBox : {trimTopSpin, trimBottomSpin, trimLeftSpin, trimRightSpin}) {
    spinBox->setDecimals(decimals);
    spinBox->setSingleStep(step);
    spinBox->setSuffix(unitSuffix);
    core::Utils::setNumberFieldWidth(spinBox, widestValue);
  }
  pullTrimToControls();
}

OrthogonalRotation OptionsWidget::rotation() const {
  return m_rotation;
}

ImageTrim OptionsWidget::trim() const {
  return m_settings->getTrim(m_pageId.imageId());
}

void OptionsWidget::rotateLeft() {
  OrthogonalRotation rotation(m_rotation);
  rotation.prevClockwiseDirection();
  setRotation(rotation);
}

void OptionsWidget::rotateRight() {
  OrthogonalRotation rotation(m_rotation);
  rotation.nextClockwiseDirection();
  setRotation(rotation);
}

void OptionsWidget::showApplyToDialog() {
  auto* dialog = new ApplyDialog(this, m_pageId, m_pageSelectionAccessor);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &ApplyDialog::appliedTo, this, &OptionsWidget::appliedTo);
  dialog->show();
}

void OptionsWidget::appliedTo(const std::set<PageId>& pages, const bool applyRotation, const bool applyTrim) {
  if (pages.empty()) {
    return;
  }

  if (applyRotation) {
    m_settings->applyRotation(pages, m_rotation);
  }
  if (applyTrim) {
    // The fields refer to the view, so each page gets them turned back by its own rotation.
    const ImageTrim viewTrim(currentTrimFromControls());
    // A length the fields show in mm, cm or inches stays that length on a page of another
    // resolution. Pixels stay pixels.
    std::map<ImageId, Dpi> pageDpis;
    if (fieldUnits() != PIXELS) {
      const PageSequence allPages(m_pageSelectionAccessor.allPages());
      for (size_t i = 0; i < allPages.numPages(); ++i) {
        const PageInfo& info = allPages.pageAt(i);
        pageDpis[info.imageId()] = info.metadata().dpi();
      }
    }
    for (const PageId& page : pages) {
      const OrthogonalRotation pageRotation(m_settings->getRotationFor(page.imageId()));
      ImageTrim pageTrim(viewTrim);
      const auto dpiIt = pageDpis.find(page.imageId());
      if (dpiIt != pageDpis.end()) {
        pageTrim = viewTrim.rescaled(viewDpi(), Dpi(pageRotation.rotate(dpiIt->second.toSize())));
      }
      m_settings->setTrim(page.imageId(), pageTrim.unrotated(pageRotation));
    }
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }
}

void OptionsWidget::setRotation(const OrthogonalRotation& rotation) {
  if (rotation == m_rotation) {
    return;
  }

  m_rotation = rotation;
  setRotationPixmap();

  m_settings->applyRotation(m_pageId.imageId(), rotation);
  {
    // The trim fields follow the view.
    auto block = m_connectionManager.getScopedBlock();
    pullTrimToControls();
  }

  emit rotationOrTrimChanged(rotation, trim());
  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::trimEnableToggled(const bool checked) {
  trimFieldsWidget->setEnabled(checked);
  pushTrimFromControls();
}

void OptionsWidget::trimMarginsChanged(const double value) {
  if (!trimEnabledCheck->isChecked()) {
    return;
  }
  // Only the changed edge is converted back. Converting all four fields would move the others by
  // a pixel when the shown value is rounded, e.g. 30 px at 600 dpi are shown as 1.3 mm.
  const QSize viewSize(m_rotation.rotate(m_imagePixelSize));
  const int maxHorizontal = std::max(0, viewSize.width() - kMinTrimInnerSide);
  const int maxVertical = std::max(0, viewSize.height() - kMinTrimInnerSide);
  const QObject* const field = sender();
  if (field == trimLeftSpin) {
    m_viewTrim.left
        = std::clamp(fieldToPixels(value, Qt::Horizontal), 0, std::max(0, maxHorizontal - m_viewTrim.right));
  } else if (field == trimRightSpin) {
    m_viewTrim.right
        = std::clamp(fieldToPixels(value, Qt::Horizontal), 0, std::max(0, maxHorizontal - m_viewTrim.left));
  } else if (field == trimTopSpin) {
    m_viewTrim.top = std::clamp(fieldToPixels(value, Qt::Vertical), 0, std::max(0, maxVertical - m_viewTrim.bottom));
  } else if (field == trimBottomSpin) {
    m_viewTrim.bottom = std::clamp(fieldToPixels(value, Qt::Vertical), 0, std::max(0, maxVertical - m_viewTrim.top));
  }
  {
    auto block = m_connectionManager.getScopedBlock();
    updateTrimMaximums();
  }
  pushTrimFromControls();
}

Dpi OptionsWidget::viewDpi() const {
  return Dpi(m_rotation.rotate(m_imageDpi.toSize()));
}

Units OptionsWidget::fieldUnits() const {
  return m_imageDpi.isNull() ? PIXELS : UnitsProvider::getInstance().getUnits();
}

double OptionsWidget::pixelsToField(const int pixels, const Qt::Orientation direction) const {
  double horizontal = pixels;
  double vertical = pixels;
  UnitsConverter(viewDpi()).convert(horizontal, vertical, PIXELS, fieldUnits());
  return (direction == Qt::Horizontal) ? horizontal : vertical;
}

int OptionsWidget::fieldToPixels(const double value, const Qt::Orientation direction) const {
  double horizontal = value;
  double vertical = value;
  UnitsConverter(viewDpi()).convert(horizontal, vertical, fieldUnits(), PIXELS);
  return static_cast<int>(std::lround((direction == Qt::Horizontal) ? horizontal : vertical));
}

void OptionsWidget::setTrimFieldMaximum(QDoubleSpinBox* spinBox, const int pixels, const Qt::Orientation direction) {
  spinBox->setMaximum(pixelsToField(std::max(0, pixels), direction));
}

void OptionsWidget::updateTrimMaximums() {
  const QSize viewSize(m_rotation.rotate(m_imagePixelSize));
  const int maxHorizontal = std::max(0, viewSize.width() - kMinTrimInnerSide);
  const int maxVertical = std::max(0, viewSize.height() - kMinTrimInnerSide);

  setTrimFieldMaximum(trimLeftSpin, maxHorizontal - m_viewTrim.right, Qt::Horizontal);
  setTrimFieldMaximum(trimRightSpin, maxHorizontal - m_viewTrim.left, Qt::Horizontal);
  setTrimFieldMaximum(trimTopSpin, maxVertical - m_viewTrim.bottom, Qt::Vertical);
  setTrimFieldMaximum(trimBottomSpin, maxVertical - m_viewTrim.top, Qt::Vertical);
}

void OptionsWidget::pullTrimToControls() {
  ImageTrim trim(m_settings->getTrim(m_pageId.imageId()).rotated(m_rotation));
  const QSize viewSize(m_rotation.rotate(m_imagePixelSize));
  if (trim.enabled && viewSize.isValid()) {
    // Show the values that are used. A trim that does not fit the image (e.g. after the image was replaced by
    // a smaller one) is redistributed by toInnerRect(); cutting it to the field maximums would show other values.
    const QRect inner(trim.toInnerRect(viewSize));
    trim.left = inner.left();
    trim.top = inner.top();
    trim.right = viewSize.width() - inner.left() - inner.width();
    trim.bottom = viewSize.height() - inner.top() - inner.height();
  }
  m_viewTrim = trim;
  trimEnabledCheck->setChecked(trim.enabled);
  // The maximums come from the pixels, so they never cut the values of the page shown before.
  updateTrimMaximums();
  trimLeftSpin->setValue(pixelsToField(trim.left, Qt::Horizontal));
  trimRightSpin->setValue(pixelsToField(trim.right, Qt::Horizontal));
  trimTopSpin->setValue(pixelsToField(trim.top, Qt::Vertical));
  trimBottomSpin->setValue(pixelsToField(trim.bottom, Qt::Vertical));
  trimFieldsWidget->setEnabled(trim.enabled);
}

void OptionsWidget::pushTrimFromControls() {
  const ImageTrim trim(currentTrimFromControls().unrotated(m_rotation));
  m_settings->setTrim(m_pageId.imageId(), trim);
  emit rotationOrTrimChanged(m_rotation, trim);
  emit invalidateThumbnail(m_pageId);
}

ImageTrim OptionsWidget::currentTrimFromControls() const {
  ImageTrim trim;
  trim.enabled = trimEnabledCheck->isChecked();
  if (!trim.enabled) {
    return trim;
  }
  trim.left = m_viewTrim.left;
  trim.right = m_viewTrim.right;
  trim.top = m_viewTrim.top;
  trim.bottom = m_viewTrim.bottom;
  return trim;
}

void OptionsWidget::setRotationPixmap() {
  QIcon icon;
  switch (m_rotation.toDegrees()) {
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
  rotationIndicator->setPixmap(icon.pixmap(32, 32));
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidget::setupUiConnections() {
  CONNECT(rotateLeftBtn, &QAbstractButton::clicked, this, &OptionsWidget::rotateLeft);
  CONNECT(rotateRightBtn, &QAbstractButton::clicked, this, &OptionsWidget::rotateRight);
  CONNECT(applyToBtn, &QAbstractButton::clicked, this, &OptionsWidget::showApplyToDialog);
  CONNECT(trimEnabledCheck, &QAbstractButton::toggled, this, &OptionsWidget::trimEnableToggled);
  CONNECT(trimLeftSpin, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimRightSpin, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimTopSpin, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimBottomSpin, &QDoubleSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
}

#undef CONNECT

void OptionsWidget::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  rotateLeftBtn->setIcon(iconProvider.getIcon("object-rotate-left"));
  rotateRightBtn->setIcon(iconProvider.getIcon("object-rotate-right"));
}
}  // namespace fix_orientation
