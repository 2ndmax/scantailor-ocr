// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_APPLYDIALOG_H_
#define SCANTAILOR_PAGE_LAYOUT_APPLYDIALOG_H_

#include <QDialog>
#include <set>

#include "PageId.h"
#include "ui_ApplyDialog.h"

class PageSelectionAccessor;

namespace page_layout {
class ApplyDialog : public QDialog, private Ui::ApplyDialog {
  Q_OBJECT
 public:
  ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor);

  ~ApplyDialog() override;

 signals:

  void accepted(const std::set<PageId>& pages);

 private slots:

  void onSubmit();
};
}  // namespace page_layout
#endif  // ifndef SCANTAILOR_PAGE_LAYOUT_APPLYDIALOG_H_
