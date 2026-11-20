// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_OPTIONSWIDGET_H_
#define SCANTAILOR_SELECT_CONTENT_OPTIONSWIDGET_H_

#include <AutoManualMode.h>
#include <ConnectionManager.h>
#include <UnitsListener.h>

#include <QRectF>
#include <QSizeF>
#include <list>
#include <memory>

#include "../Dependencies.h"
#include "../Params.h"
#include "../PhysSizeCalc.h"
#include "../UiData.h"
#include "FilterOptionsWidget.h"
#include "PageId.h"
#include "PageSelectionAccessor.h"
#include "ui_OptionsWidget.h"

namespace select_content {
class SelectContentParams;
}

namespace select_content {
class Settings;

class OptionsWidget : public FilterOptionsWidget, public UnitsListener, private Ui::OptionsWidget {
  Q_OBJECT
 public:
  OptionsWidget(std::shared_ptr<Settings> settings,
                std::shared_ptr<const select_content::SelectContentParams> defaultParams,
                const PageSelectionAccessor& pageSelectionAccessor);

  ~OptionsWidget() override;

  void preUpdateUI(const PageInfo& pageInfo);

  void postUpdateUI(const UiData& uiData);

  void onUnitsChanged(Units units) override;

 public slots:

  void manualContentRectSet(const QRectF& contentRect);

  void manualPageRectSet(const QRectF& pageRect);

  void updatePageRectSize(const QSizeF& size);

 signals:

  void pageRectChangedLocally(const QRectF& pageRect);

  void pageRectStateChanged(bool state);

 private slots:

  void showApplyToDialog();

  void applySelection(const std::set<PageId>& pages, bool applyContentBox, bool applyPageBox);

  void contentDetectToggled(AutoManualMode mode);

  void pageDetectToggled(AutoManualMode mode);

  void fineTuningChanged(bool checked);

  void detectionOptionsChanged();

  void dimensionsChangedLocally(double);

 private:
  void updateContentModeIndication(AutoManualMode mode);

  void updatePageModeIndication(AutoManualMode mode);

  void updatePageDetectOptionsDisplay();

  void updateContentDetectOptionsDisplay();

  void commitCurrentParams();

  void updateDependenciesIfNecessary();

  void setupUiConnections();

  std::shared_ptr<Settings> m_settings;
  // The start values of the project, for pages without detection settings of their own.
  std::shared_ptr<const select_content::SelectContentParams> m_defaultParams;
  UiData m_uiData;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  Dpi m_dpi;

  ConnectionManager m_connectionManager;
};


}  // namespace select_content
#endif  // ifndef SCANTAILOR_SELECT_CONTENT_OPTIONSWIDGET_H_
