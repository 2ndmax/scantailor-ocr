// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_APPLYDIALOG_H_
#define SCANTAILOR_SELECT_CONTENT_APPLYDIALOG_H_

#include <QDialog>
#include <set>

#include "PageId.h"
#include "ui_ApplyDialog.h"

class PageSelectionAccessor;

namespace select_content {
class ApplyDialog : public QDialog, private Ui::ApplyDialog {
  Q_OBJECT
 public:
  ApplyDialog(QWidget* parent, const PageId& curPage, const PageSelectionAccessor& pageSelectionAccessor);

  ~ApplyDialog() override;

 signals:

  void applySelection(const std::set<PageId>& pages, bool applyContentBox, bool applyPageBox);

 private slots:

  void onSubmit();
};
}  // namespace select_content
#endif  // ifndef SCANTAILOR_SELECT_CONTENT_APPLYDIALOG_H_
