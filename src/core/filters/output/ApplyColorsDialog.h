// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_APPLYCOLORSDIALOG_H_
#define SCANTAILOR_OUTPUT_APPLYCOLORSDIALOG_H_

#include <QDialog>
#include <set>

#include "PageId.h"
#include "ui_ApplyColorsDialog.h"

class PageSelectionAccessor;

namespace output {
class ApplyColorsDialog : public QDialog, private Ui::ApplyColorsDialog {
  Q_OBJECT
 public:
  ApplyColorsDialog(QWidget* parent, const PageId& pageId, const PageSelectionAccessor& pageSelectionAccessor);

  ~ApplyColorsDialog() override;

  /** Shows the choice of the parts to apply: the color mode and the other settings of the Mode group. */
  void showModeParts();

  bool isColorModeChosen() const;

  bool isOtherSettingsChosen() const;

  /**
   * For actions that have not been done on the current page yet: "This page only" then gives the
   * current page instead of no page.
   */
  void setThisPageIncluded(bool included);

 signals:

  void accepted(const std::set<PageId>& pages);

 private slots:

  void onSubmit();

 private:
  PageId m_curPage;
  bool m_thisPageIncluded = false;
  bool m_modeParts = false;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_APPLYCOLORSDIALOG_H_
