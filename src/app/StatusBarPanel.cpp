// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "StatusBarPanel.h"

#include <core/IconProvider.h>

#include <QActionGroup>  // In QtGui with Qt 6, in QtWidgets with Qt 5.
#include <QMenu>
#include <QtCore/QFileInfo>
#include <cmath>

#include "ImageViewInfoProvider.h"
#include "PageId.h"
#include "UnitsProvider.h"

StatusBarPanel::StatusBarPanel() {
  ui.setupUi(this);
  setupZoneModeMenu();
  ui.zoneModeButton->hide();
  ui.zoneModeLine->hide();
}

void StatusBarPanel::setupZoneModeMenu() {
  auto* menu = new QMenu(ui.zoneModeButton);
  auto* group = new QActionGroup(menu);
  const auto addModeAction = [&](const QString& text, const char* iconName, ZoneCreationMode mode) {
    QAction* action = menu->addAction(IconProvider::getInstance().getIcon(iconName), text);
    action->setCheckable(true);
    group->addAction(action);
    connect(action, &QAction::triggered, this, [this, mode]() {
      if (m_setZoneMode) {
        m_setZoneMode(mode);
      }
    });
    return action;
  };
  m_polygonalAction = addModeAction(tr("Polygon selection (Z)"), "polygonal-zone-mode", ZoneCreationMode::POLYGONAL);
  m_lassoAction = addModeAction(tr("Lasso selection (X)"), "lasso-zone-mode", ZoneCreationMode::LASSO);
  m_rectangularAction
      = addModeAction(tr("Rectangle selection (C)"), "rectangular-zone-mode", ZoneCreationMode::RECTANGULAR);
  ui.zoneModeButton->setMenu(menu);
}

QAction* StatusBarPanel::zoneModeAction(const ZoneCreationMode mode) const {
  switch (mode) {
    case ZoneCreationMode::LASSO:
      return m_lassoAction;
    case ZoneCreationMode::RECTANGULAR:
      return m_rectangularAction;
    default:
      return m_polygonalAction;
  }
}

void StatusBarPanel::onMousePosChanged(const QPointF& mousePos) {
  StatusBarPanel::m_mousePos = mousePos;
  mousePosChanged();
}

void StatusBarPanel::onPhysSizeChanged(const QSizeF& physSize) {
  StatusBarPanel::m_physSize = physSize;
  physSizeChanged();
}

void StatusBarPanel::onDpiChanged(const Dpi& dpi) {
  StatusBarPanel::m_dpi = dpi;
}

void StatusBarPanel::onImageViewInfoProviderStopped() {
  onMousePosChanged(QPointF());
  onPhysSizeChanged(QRectF().size());
  m_dpi = Dpi();
}

void StatusBarPanel::updatePage(int pageNumber, size_t pageCount, const PageId& pageId) {
  ui.pageNoLabel->setText(tr("p. %1 / %2").arg(pageNumber).arg(pageCount));
  ui.pageNoLabel->setVisible(true);

  // At most this many characters; a longer name is shortened at the front, as its end usually
  // holds the number.  The full name is in the tooltip.
  const int maxChars = 50;
  const QFileInfo fileInfo(pageId.imageId().filePath());
  QString pageFileInfo = fileInfo.completeBaseName();
  QString subPageSuffix;
  if (pageId.subPage() != PageId::SINGLE_PAGE) {
    subPageSuffix = (pageId.subPage() == PageId::LEFT_PAGE) ? tr(" [L]") : tr(" [R]");
  }
  const int nameChars = maxChars - subPageSuffix.size();
  if (pageFileInfo.size() > nameChars) {
    pageFileInfo = "..." + pageFileInfo.right(nameChars - 3);
  }

  ui.pageInfoLine->setVisible(true);
  ui.pageInfoLabel->setText(pageFileInfo + subPageSuffix);
  ui.pageInfoLabel->setToolTip(fileInfo.fileName() + subPageSuffix);
  ui.pageInfoLabel->setVisible(true);
}

namespace {
inline void clearAndHideLabel(QLabel* widget) {
  widget->clear();
  widget->hide();
}
}  // namespace

void StatusBarPanel::clear() {
  clearAndHideLabel(ui.mousePosLabel);
  clearAndHideLabel(ui.physSizeLabel);
  clearAndHideLabel(ui.pageNoLabel);
  clearAndHideLabel(ui.pageInfoLabel);
  ui.zoneModeButton->hide();

  ui.mousePosLine->setVisible(false);
  ui.physSizeLine->setVisible(false);
  ui.pageInfoLine->setVisible(false);
  ui.zoneModeLine->setVisible(false);
}

void StatusBarPanel::onUnitsChanged(Units) {
  mousePosChanged();
  physSizeChanged();
}

void StatusBarPanel::mousePosChanged() {
  if (!m_mousePos.isNull() && !m_dpi.isNull()) {
    double x = m_mousePos.x();
    double y = m_mousePos.y();
    UnitsProvider::getInstance().convertFrom(x, y, PIXELS, m_dpi);

    switch (UnitsProvider::getInstance().getUnits()) {
      case PIXELS:
      case MILLIMETRES:
        x = std::ceil(x);
        y = std::ceil(y);
        break;
      default:
        x = std::ceil(x * 10) / 10;
        y = std::ceil(y * 10) / 10;
        break;
    }

    ui.mousePosLine->setVisible(true);
    ui.mousePosLabel->setText(QString("%1, %2").arg(x).arg(y));
    ui.mousePosLabel->setVisible(true);
  } else {
    clearAndHideLabel(ui.mousePosLabel);
    ui.mousePosLine->setVisible(false);
  }
}

void StatusBarPanel::physSizeChanged() {
  if (!m_physSize.isNull() && !m_dpi.isNull()) {
    double width = m_physSize.width();
    double height = m_physSize.height();
    UnitsProvider::getInstance().convertFrom(width, height, PIXELS, m_dpi);

    const Units units = UnitsProvider::getInstance().getUnits();
    switch (units) {
      case PIXELS:
        width = std::round(width);
        height = std::round(height);
        break;
      case MILLIMETRES:
        width = std::round(width);
        height = std::round(height);
        break;
      case CENTIMETRES:
        width = std::round(width * 10) / 10;
        height = std::round(height * 10) / 10;
        break;
      case INCHES:
        width = std::round(width * 10) / 10;
        height = std::round(height * 10) / 10;
        break;
    }

    ui.physSizeLine->setVisible(true);
    // The resolution of the image shown: the source resolution, in Output the output resolution.
    const QString dpiText = (m_dpi.horizontal() == m_dpi.vertical())
                                ? QString("%1 dpi").arg(m_dpi.horizontal())
                                : QString("%1 x %2 dpi").arg(m_dpi.horizontal()).arg(m_dpi.vertical());
    ui.physSizeLabel->setText(
        QString("%1 x %2 %3 / %4").arg(width).arg(height).arg(unitsToLocalizedString(units)).arg(dpiText));
    ui.physSizeLabel->setVisible(true);
  } else {
    clearAndHideLabel(ui.physSizeLabel);
    ui.physSizeLine->setVisible(false);
  }
}

void StatusBarPanel::onZoneModeProviderStarted(const std::function<void(ZoneCreationMode)>& setMode) {
  m_setZoneMode = setMode;
}

void StatusBarPanel::onZoneModeChanged(ZoneCreationMode mode) {
  switch (mode) {
    case ZoneCreationMode::RECTANGULAR:
      ui.zoneModeButton->setIcon(IconProvider::getInstance().getIcon("rectangular-zone-mode"));
      ui.zoneModeButton->setText(tr("Rectangle selection"));
      break;
    case ZoneCreationMode::LASSO:
      ui.zoneModeButton->setIcon(IconProvider::getInstance().getIcon("lasso-zone-mode"));
      ui.zoneModeButton->setText(tr("Lasso selection"));
      break;
    case ZoneCreationMode::POLYGONAL:
      ui.zoneModeButton->setIcon(IconProvider::getInstance().getIcon("polygonal-zone-mode"));
      ui.zoneModeButton->setText(tr("Polygon selection"));
      break;
  }
  zoneModeAction(mode)->setChecked(true);
  ui.zoneModeButton->setVisible(true);
  ui.zoneModeLine->setVisible(true);
}

void StatusBarPanel::onZoneModeProviderStopped() {
  m_setZoneMode = nullptr;
  ui.zoneModeButton->hide();
  ui.zoneModeLine->setVisible(false);
}
