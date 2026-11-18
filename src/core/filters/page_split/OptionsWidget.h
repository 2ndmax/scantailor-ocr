// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_OPTIONSWIDGET_H_
#define SCANTAILOR_PAGE_SPLIT_OPTIONSWIDGET_H_

#include <core/ConnectionManager.h>

#include <list>
#include <memory>
#include <set>

#include "AutoManualMode.h"
#include "Dependencies.h"
#include "FilterOptionsWidget.h"
#include "ImageId.h"
#include "LayoutType.h"
#include "PageId.h"
#include "PageLayout.h"
#include "PageSelectionAccessor.h"
#include "SplitDetectionMethod.h"
#include "UiData.h"
#include "ui_OptionsWidget.h"

class ProjectPages;

namespace page_split {
class Settings;

class OptionsWidget : public FilterOptionsWidget, private Ui::OptionsWidget {
  Q_OBJECT
 public:
  OptionsWidget(std::shared_ptr<Settings> settings,
                std::shared_ptr<ProjectPages> pageSequence,
                const PageSelectionAccessor& pageSelectionAccessor);

  ~OptionsWidget() override;

  void preUpdateUI(const PageId& pageId);

  void postUpdateUI(const UiData& uiData);

 signals:

  void pageLayoutSetLocally(const PageLayout& pageLayout);

 public slots:

  void pageLayoutSetExternally(const PageLayout& pageLayout);

 private slots:

  void layoutTypeButtonToggled(bool checked);

  void layoutAutoToggled(bool checked);

  void layoutManualToggled(bool checked);

  void showApplyDialog();

  void splitLineModeChanged(bool autoMode);

  void detectionMethodChanged(int index);

  /** Sets which half of a split page comes first, for the whole project. */
  void pageOrderToggled(bool rightFirst);

 private:
  /**
   * \param sourcePage The page whose split line is copied when \p applyCut is set.
   *        Nothing is changed if that page has no split line.
   */
  void layoutTypeSet(const PageId& sourcePage, const std::set<PageId>& pages, LayoutType layoutType, bool applyCut);

  /**
   * Sets the detection method of the pages. With \p redetect, a split line that was
   * detected automatically with another method is detected again; one set by hand stays.
   */
  void detectionMethodSet(const std::set<PageId>& pages, SplitDetectionMethod method, bool redetect = true);

  /** Shows whether the page type is detected automatically or set by hand. */
  void setLayoutModeButtons(bool autoMode);

  void commitCurrentParams();

  void setupUiConnections();

  void setupIcons();

  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<ProjectPages> m_pages;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  UiData m_uiData;
  // False between preUpdateUI() and postUpdateUI(): m_uiData still belongs to the previous page.
  bool m_uiDataValid = false;

  ConnectionManager m_connectionManager;
};


}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_OPTIONSWIDGET_H_
