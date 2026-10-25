// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <core/IconProvider.h>

#include <QSize>

#include <algorithm>
#include <cassert>
#include <utility>

#include "../../Utils.h"
#include "ApplyDialog.h"
#include "Filter.h"
#include "ProjectPages.h"
#include "Settings.h"

namespace fix_orientation {

OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  // The trim fields are as wide as "9999 px".
  for (QSpinBox* spinBox : {trimTopSpin, trimBottomSpin, trimLeftSpin, trimRightSpin}) {
    core::Utils::setNumberFieldWidth(spinBox, 9999);
  }
  setupIcons();

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageInfo& pageInfo, const OrthogonalRotation rotation) {
  auto block = m_connectionManager.getScopedBlock();

  m_pageId = pageInfo.id();
  m_imagePixelSize = pageInfo.metadata().size();
  m_rotation = rotation;
  setRotationPixmap();
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
    for (const PageId& page : pages) {
      m_settings->setTrim(page.imageId(), viewTrim.unrotated(m_settings->getRotationFor(page.imageId())));
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

void OptionsWidget::trimMarginsChanged(const int) {
  if (!trimEnabledCheck->isChecked()) {
    return;
  }
  updateTrimMaximums();
  pushTrimFromControls();
}


void OptionsWidget::updateTrimMaximums() {
  const QSize viewSize(m_rotation.rotate(m_imagePixelSize));
  const int maxHorizontal = std::max(0, viewSize.width() - kMinTrimInnerSide);
  const int maxVertical = std::max(0, viewSize.height() - kMinTrimInnerSide);

  trimLeftSpin->setMaximum(std::max(0, maxHorizontal - trimRightSpin->value()));
  trimRightSpin->setMaximum(std::max(0, maxHorizontal - trimLeftSpin->value()));
  trimTopSpin->setMaximum(std::max(0, maxVertical - trimBottomSpin->value()));
  trimBottomSpin->setMaximum(std::max(0, maxVertical - trimTopSpin->value()));
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
  trimEnabledCheck->setChecked(trim.enabled);
  // Each maximum depends on the opposite field, so the values are set against the full ranges.
  for (QSpinBox* spinBox : {trimLeftSpin, trimRightSpin, trimTopSpin, trimBottomSpin}) {
    spinBox->setValue(0);
  }
  updateTrimMaximums();
  trimLeftSpin->setValue(trim.left);
  trimRightSpin->setValue(trim.right);
  trimTopSpin->setValue(trim.top);
  trimBottomSpin->setValue(trim.bottom);
  updateTrimMaximums();
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
  trim.left = trimLeftSpin->value();
  trim.right = trimRightSpin->value();
  trim.top = trimTopSpin->value();
  trim.bottom = trimBottomSpin->value();
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
  CONNECT(trimLeftSpin, &QSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimRightSpin, &QSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimTopSpin, &QSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
  CONNECT(trimBottomSpin, &QSpinBox::valueChanged, this, &OptionsWidget::trimMarginsChanged);
}

#undef CONNECT

void OptionsWidget::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  rotateLeftBtn->setIcon(iconProvider.getIcon("object-rotate-left"));
  rotateRightBtn->setIcon(iconProvider.getIcon("object-rotate-right"));
}
}  // namespace fix_orientation
