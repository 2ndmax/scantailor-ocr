// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_STATUSBARPANEL_H_
#define SCANTAILOR_APP_STATUSBARPANEL_H_

#include <core/ZoneModeListener.h>

#include <QtCore/QMutex>
#include <QtWidgets/QWidget>
#include <functional>

#include "Dpi.h"
#include "ImageViewInfoListener.h"
#include "ImageViewInfoProvider.h"
#include "UnitsListener.h"
#include "ui_StatusBarPanel.h"

class PageId;
class QAction;

class StatusBarPanel : public QWidget, public UnitsListener, public ImageViewInfoListener, public ZoneModeListener {
  Q_OBJECT
 public:
  StatusBarPanel();

  ~StatusBarPanel() override = default;

 public:
  void onMousePosChanged(const QPointF& mousePos) override;

  void onPhysSizeChanged(const QSizeF& physSize) override;

  void onDpiChanged(const Dpi& dpi) override;

  void onImageViewInfoProviderStopped() override;

  void updatePage(int pageNumber, size_t pageCount, const PageId& pageId);

  void clear();

  void onUnitsChanged(Units) override;

  void onZoneModeProviderStarted(const std::function<void(ZoneCreationMode)>& setMode) override;

  void onZoneModeChanged(ZoneCreationMode mode) override;

  void onZoneModeProviderStopped() override;

 private:
  void mousePosChanged();

  void physSizeChanged();

  void setupZoneModeMenu();

  QAction* zoneModeAction(ZoneCreationMode mode) const;

  Ui::StatusBarPanel ui;
  QAction* m_polygonalAction = nullptr;
  QAction* m_lassoAction = nullptr;
  QAction* m_rectangularAction = nullptr;
  std::function<void(ZoneCreationMode)> m_setZoneMode;
  QPointF m_mousePos;
  QSizeF m_physSize;
  Dpi m_dpi;
};


#endif  // SCANTAILOR_APP_STATUSBARPANEL_H_
