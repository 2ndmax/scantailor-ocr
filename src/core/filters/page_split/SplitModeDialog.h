// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_SPLITMODEDIALOG_H_
#define SCANTAILOR_PAGE_SPLIT_SPLITMODEDIALOG_H_

#include <QButtonGroup>
#include <QDialog>
#include <memory>
#include <set>

#include "LayoutType.h"
#include "PageId.h"
#include "PageSequence.h"
#include "ui_SplitModeDialog.h"

class ProjectPages;
class PageSelectionAccessor;

namespace page_split {
/**
 * Applies the page type of the current page to other pages, and optionally its split line.
 * Whether the page type is detected automatically or set by hand is chosen in the options panel.
 */
class SplitModeDialog : public QDialog, private Ui::SplitModeDialog {
  Q_OBJECT
 public:
  SplitModeDialog(QWidget* parent,
                  const PageId& curPage,
                  const PageSelectionAccessor& pageSelectionAccessor,
                  LayoutType layoutType);

  ~SplitModeDialog() override;

 signals:

  void accepted(const std::set<PageId>& pages, LayoutType layoutType, bool applyCut);

 private slots:

  void onSubmit();

 private:
  void updateOptions();

  PageSequence m_pages;
  std::set<PageId> m_selectedPages;
  PageId m_curPage;
  QButtonGroup* m_scopeGroup;
  LayoutType m_layoutType;
};
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_SPLITMODEDIALOG_H_
