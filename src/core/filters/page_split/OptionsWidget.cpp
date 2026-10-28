// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidget.h"

#include <core/IconProvider.h>

#include <QButtonGroup>
#include <QSignalBlocker>
#include <algorithm>
#include <cassert>
#include <utility>

#include "Filter.h"
#include "PageLayoutAdapter.h"
#include "ProjectPages.h"
#include "ScopedIncDec.h"
#include "Settings.h"
#include "SplitModeDialog.h"

namespace page_split {
OptionsWidget::OptionsWidget(std::shared_ptr<Settings> settings,
                             std::shared_ptr<ProjectPages> pageSequence,
                             const PageSelectionAccessor& pageSelectionAccessor)
    : m_settings(std::move(settings)),
      m_pages(std::move(pageSequence)),
      m_pageSelectionAccessor(pageSelectionAccessor),
      m_connectionManager(std::bind(&OptionsWidget::setupUiConnections, this)) {
  setupUi(this);
  setupIcons();

  // Workaround for QTBUG-182
  auto* grp = new QButtonGroup(this);
  grp->addButton(autoBtn);
  grp->addButton(manualBtn);
  auto* layoutModeGroup = new QButtonGroup(this);
  layoutModeGroup->addButton(layoutAutoBtn);
  layoutModeGroup->addButton(layoutManualBtn);
  auto* pageOrderButtons = new QButtonGroup(this);
  pageOrderButtons->addButton(leftFirstBtn);
  pageOrderButtons->addButton(rightFirstBtn);

  detectionMethodBox->addItem(tr("Edges"), EDGES_DETECTION);
  detectionMethodBox->addItem(tr("Spine line"), SPINE_LINE_DETECTION);

  setupUiConnections();
}

OptionsWidget::~OptionsWidget() = default;

void OptionsWidget::preUpdateUI(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  m_pageId = pageId;
  m_uiDataValid = false;
  const Settings::Record record(m_settings->getPageRecord(pageId.imageId()));
  const LayoutType layoutType(record.combinedLayoutType());

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

  splitLineGroup->setEnabled(layoutType != SINGLE_PAGE_UNCUT);

  // Until the page is processed, the detected page type isn't known.
  setLayoutModeButtons(layoutType == AUTO_LAYOUT_TYPE);
  layoutAutoBtn->setEnabled(false);
  layoutManualBtn->setEnabled(false);
  applyToBtn->setEnabled(layoutType != AUTO_LAYOUT_TYPE);

  // Uncheck both the Auto and Manual buttons.
  autoBtn->setChecked(true);
  autoBtn->setAutoExclusive(false);
  autoBtn->setChecked(false);
  autoBtn->setAutoExclusive(true);
  // And disable both of them.
  autoBtn->setEnabled(false);
  manualBtn->setEnabled(false);

  detectionMethodBox->setCurrentIndex(detectionMethodBox->findData(m_settings->detectionMethod(pageId.imageId())));

  const bool rightFirst = (m_pages->layoutDirection() == Qt::RightToLeft);
  rightFirstBtn->setChecked(rightFirst);
  leftFirstBtn->setChecked(!rightFirst);
}  // OptionsWidget::preUpdateUI

void OptionsWidget::postUpdateUI(const UiData& uiData) {
  auto block = m_connectionManager.getScopedBlock();

  m_uiData = uiData;
  m_uiDataValid = true;

  applyToBtn->setEnabled(true);
  layoutAutoBtn->setEnabled(true);
  layoutManualBtn->setEnabled(true);
  autoBtn->setEnabled(true);
  manualBtn->setEnabled(true);

  if (uiData.splitLineMode() == MODE_AUTO) {
    autoBtn->setChecked(true);
  } else {
    manualBtn->setChecked(true);
  }

  const PageLayout::Type layoutType = uiData.pageLayout().type();

  switch (layoutType) {
    case PageLayout::SINGLE_PAGE_UNCUT:
      singlePageUncutBtn->setChecked(true);
      break;
    case PageLayout::SINGLE_PAGE_CUT:
      pagePlusOffcutBtn->setChecked(true);
      break;
    case PageLayout::TWO_PAGES:
      twoPagesBtn->setChecked(true);
      break;
  }

  splitLineGroup->setEnabled(layoutType != PageLayout::SINGLE_PAGE_UNCUT);

  setLayoutModeButtons(uiData.layoutTypeAutoDetected());
}  // OptionsWidget::postUpdateUI

void OptionsWidget::setLayoutModeButtons(const bool autoMode) {
  const QSignalBlocker autoBlocker(layoutAutoBtn);
  const QSignalBlocker manualBlocker(layoutManualBtn);
  if (autoMode) {
    layoutAutoBtn->setChecked(true);
  } else {
    layoutManualBtn->setChecked(true);
  }
}

void OptionsWidget::layoutAutoToggled(const bool checked) {
  if (checked) {
    layoutTypeSet(m_pageId, {m_pageId}, AUTO_LAYOUT_TYPE, false);
  }
}

void OptionsWidget::layoutManualToggled(const bool checked) {
  if (checked) {
    // Keeps the page type shown, which was detected automatically.
    layoutTypeSet(m_pageId, {m_pageId}, m_uiData.pageLayout().toLayoutType(), false);
  }
}

void OptionsWidget::pageLayoutSetExternally(const PageLayout& pageLayout) {
  auto block = m_connectionManager.getScopedBlock();

  m_uiData.setPageLayout(pageLayout);
  m_uiData.setSplitLineMode(MODE_MANUAL);
  commitCurrentParams();

  manualBtn->setChecked(true);

  emit invalidateThumbnail(m_pageId);
}

void OptionsWidget::layoutTypeButtonToggled(const bool checked) {
  if (!checked) {
    return;
  }

  LayoutType lt;
  ProjectPages::LayoutType plt = ProjectPages::ONE_PAGE_LAYOUT;

  QObject* button = sender();
  if (button == singlePageUncutBtn) {
    lt = SINGLE_PAGE_UNCUT;
  } else if (button == pagePlusOffcutBtn) {
    lt = PAGE_PLUS_OFFCUT;
  } else {
    assert(button == twoPagesBtn);
    lt = TWO_PAGES;
    plt = ProjectPages::TWO_PAGE_LAYOUT;
  }

  Settings::UpdateAction update;
  update.setLayoutType(lt);

  splitLineGroup->setEnabled(lt != SINGLE_PAGE_UNCUT);
  setLayoutModeButtons(false);

  m_pages->setLayoutTypeFor(m_pageId.imageId(), plt);

  // While the page is still being processed, its split line isn't known yet,
  // so only the page type is stored and the page is processed again.
  // The same from "page plus offcut" to "two pages": neither cut of that layout is a
  // split line (one may be the image edge), so the split line is detected anew.
  const bool fromPagePlusOffcut = m_uiDataValid && (m_uiData.pageLayout().type() == PageLayout::SINGLE_PAGE_CUT);
  if (!m_uiDataValid || (lt == PAGE_PLUS_OFFCUT) || ((lt == TWO_PAGES) && fromPagePlusOffcut)
      || ((lt != SINGLE_PAGE_UNCUT) && (m_uiData.splitLineMode() == MODE_AUTO))) {
    m_settings->updatePage(m_pageId.imageId(), update);
    emit reloadRequested();
  } else {
    PageLayout::Type newLayoutType;
    if (lt == SINGLE_PAGE_UNCUT) {
      newLayoutType = PageLayout::SINGLE_PAGE_UNCUT;
    } else {
      assert(lt == TWO_PAGES);
      newLayoutType = PageLayout::TWO_PAGES;
    }

    PageLayout newLayout(m_uiData.pageLayout());
    newLayout.setType(newLayoutType);
    const Params newParams(newLayout, m_uiData.dependencies(), m_uiData.splitLineMode());

    update.setParams(newParams);
    m_settings->updatePage(m_pageId.imageId(), update);

    m_uiData.setPageLayout(newLayout);

    emit pageLayoutSetLocally(newLayout);
    emit invalidateThumbnail(m_pageId);
  }
}  // OptionsWidget::layoutTypeButtonToggled

void OptionsWidget::showApplyDialog() {
  const Settings::Record record(m_settings->getPageRecord(m_pageId.imageId()));
  // The page type and the detection method can be applied before the page was processed,
  // only the split line needs its params.
  auto* dialog = new SplitModeDialog(this, m_pageId, m_pageSelectionAccessor, record.combinedLayoutType(),
                                     record.params() != nullptr);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  const PageId sourcePage = m_pageId;
  connect(dialog, &SplitModeDialog::accepted, this,
          [this, sourcePage](const std::set<PageId>& pages, const bool applyLayoutType, const LayoutType layoutType,
                             const bool applyCut, const bool applyDetectionMethod) {
            if (applyLayoutType) {
              layoutTypeSet(sourcePage, pages, layoutType, applyCut);
            }
            if (applyDetectionMethod) {
              // A split line position just applied stays, even if it was detected automatically.
              const bool cutApplied = applyLayoutType && applyCut;
              detectionMethodSet(pages, m_settings->detectionMethod(sourcePage.imageId()), !cutApplied);
            }
          });
  dialog->show();
}

void OptionsWidget::layoutTypeSet(const PageId& sourcePage,
                                  const std::set<PageId>& pages,
                                  const LayoutType layoutType,
                                  bool applyCut) {
  if (pages.empty()) {
    return;
  }

  const Settings::Record sourceRecord(m_settings->getPageRecord(sourcePage.imageId()));
  const Params* const params = sourceRecord.params();
  if (applyCut && (layoutType != AUTO_LAYOUT_TYPE) && (layoutType != SINGLE_PAGE_UNCUT) && !params) {
    // There is no split line to apply.
    return;
  }

  if (layoutType != AUTO_LAYOUT_TYPE) {
    for (const PageId& pageId : pages) {
      Settings::UpdateAction updateAction;
      updateAction.setLayoutType(layoutType);
      if (applyCut && (layoutType != SINGLE_PAGE_UNCUT)) {
        Params newPageParams(*params);
        const Settings::Record oldPageRecord(m_settings->getPageRecord(pageId.imageId()));
        const Params* oldPageParams = oldPageRecord.params();
        if (oldPageParams) {
          PageLayout newPageLayout = PageLayoutAdapter::adaptPageLayout(
              params->pageLayout(), oldPageParams->pageLayout().uncutOutline().boundingRect());

          updateAction.setLayoutType(newPageLayout.toLayoutType());
          newPageParams.setPageLayout(newPageLayout);

          Dependencies deps = oldPageParams->dependencies();
          deps.setLayoutType(newPageLayout.toLayoutType());
          newPageParams.setDependencies(deps);
        }
        updateAction.setParams(newPageParams);
      }
      m_settings->updatePage(pageId.imageId(), updateAction);
    }
  } else {
    for (const PageId& pageId : pages) {
      Settings::UpdateAction updateParams;
      updateParams.setLayoutType(layoutType);
      m_settings->updatePage(pageId.imageId(), updateParams);
    }
  }

  if (pages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : pages) {
      emit invalidateThumbnail(pageId);
    }
  }

  const bool currentPageChanged = std::any_of(
      pages.begin(), pages.end(), [this](const PageId& pageId) { return pageId.imageId() == m_pageId.imageId(); });
  if (currentPageChanged) {
    setLayoutModeButtons(layoutType == AUTO_LAYOUT_TYPE);
  }

  if (layoutType == AUTO_LAYOUT_TYPE) {
    emit reloadRequested();
  }
}  // OptionsWidget::layoutTypeSet

void OptionsWidget::splitLineModeChanged(const bool autoMode) {
  if (autoMode) {
    Settings::UpdateAction update;
    update.clearParams();
    m_settings->updatePage(m_pageId.imageId(), update);
    m_uiData.setSplitLineMode(MODE_AUTO);
    emit reloadRequested();
  } else {
    m_uiData.setSplitLineMode(MODE_MANUAL);
    commitCurrentParams();
  }
}

void OptionsWidget::detectionMethodChanged(const int index) {
  detectionMethodSet({m_pageId}, (SplitDetectionMethod) detectionMethodBox->itemData(index).toInt());
}

void OptionsWidget::detectionMethodSet(const std::set<PageId>& pages,
                                       const SplitDetectionMethod method,
                                       const bool redetect) {
  std::set<PageId> redetectedPages;
  bool currentPageRedetected = false;
  for (const PageId& pageId : pages) {
    const ImageId& imageId = pageId.imageId();
    if (m_settings->detectionMethod(imageId) == method) {
      continue;
    }
    m_settings->setDetectionMethod(imageId, method);
    if (!redetect) {
      continue;
    }

    const Settings::Record record(m_settings->getPageRecord(imageId));
    const Params* const params = record.params();
    if (params && (params->splitLineMode() == MODE_MANUAL)) {
      continue;
    }
    if (params) {
      Settings::UpdateAction update;
      update.clearParams();
      m_settings->updatePage(imageId, update);
      redetectedPages.insert(pageId);
    }
    // A page still being processed may have been detected with the old method.
    if (imageId == m_pageId.imageId()) {
      currentPageRedetected = true;
    }
  }

  if (redetectedPages.size() > 1) {
    emit invalidateAllThumbnails();
  } else {
    for (const PageId& pageId : redetectedPages) {
      emit invalidateThumbnail(pageId);
    }
  }
  if (currentPageRedetected) {
    emit reloadRequested();
  }
}

void OptionsWidget::pageOrderToggled(const bool rightFirst) {
  m_pages->setLayoutDirection(rightFirst ? Qt::RightToLeft : Qt::LeftToRight);
}

void OptionsWidget::commitCurrentParams() {
  const Params params(m_uiData.pageLayout(), m_uiData.dependencies(), m_uiData.splitLineMode());
  Settings::UpdateAction update;
  update.setParams(params);
  m_settings->updatePage(m_pageId.imageId(), update);
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidget::setupUiConnections() {
  // Clicked rather than toggled: clicking the type that was detected also sets it by hand.
  CONNECT(singlePageUncutBtn, &QAbstractButton::clicked, this, &OptionsWidget::layoutTypeButtonToggled);
  CONNECT(pagePlusOffcutBtn, &QAbstractButton::clicked, this, &OptionsWidget::layoutTypeButtonToggled);
  CONNECT(twoPagesBtn, &QAbstractButton::clicked, this, &OptionsWidget::layoutTypeButtonToggled);
  CONNECT(layoutAutoBtn, &QAbstractButton::toggled, this, &OptionsWidget::layoutAutoToggled);
  CONNECT(layoutManualBtn, &QAbstractButton::toggled, this, &OptionsWidget::layoutManualToggled);
  CONNECT(applyToBtn, &QAbstractButton::clicked, this, &OptionsWidget::showApplyDialog);
  CONNECT(autoBtn, &QAbstractButton::toggled, this, &OptionsWidget::splitLineModeChanged);
  CONNECT(detectionMethodBox, &QComboBox::currentIndexChanged, this, &OptionsWidget::detectionMethodChanged);
  CONNECT(rightFirstBtn, &QAbstractButton::toggled, this, &OptionsWidget::pageOrderToggled);
}

#undef CONNECT

void OptionsWidget::setupIcons() {
  auto& iconProvider = IconProvider::getInstance();
  singlePageUncutBtn->setIcon(iconProvider.getIcon("single_page_uncut"));
  pagePlusOffcutBtn->setIcon(iconProvider.getIcon("right_page_plus_offcut"));
  twoPagesBtn->setIcon(iconProvider.getIcon("two_pages"));
  leftFirstBtn->setIcon(iconProvider.getIcon("page_order_left_first"));
  rightFirstBtn->setIcon(iconProvider.getIcon("page_order_right_first"));
}

/*============================= Widget::UiData ==========================*/

OptionsWidget::UiData::UiData() : m_splitLineMode(MODE_AUTO), m_layoutTypeAutoDetected(false) {}

OptionsWidget::UiData::~UiData() = default;
}  // namespace page_split
