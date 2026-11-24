// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>

#ifndef SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTIONDIALOG_H_
#define SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTIONDIALOG_H_

#include <QDialog>
#include <set>

#include "PageId.h"
#include "ui_OrientationDetectionDialog.h"

class PageSelectionAccessor;

namespace fix_orientation {
class OrientationDetectionDialog : public QDialog, private Ui::OrientationDetectionDialog {
  Q_OBJECT
 public:
  OrientationDetectionDialog(QWidget* parent,
                             const PageId& curPage,
                             const PageSelectionAccessor& pageSelectionAccessor);

  ~OrientationDetectionDialog() override;

  void setThisPageIncluded(bool included);

 signals:
  void accepted(const std::set<PageId>& pages, bool bookRule);

 private slots:
  void onSubmit();

 private:
  PageId m_curPage;
  bool m_thisPageIncluded = true;
};
}  // namespace fix_orientation

#endif  // SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTIONDIALOG_H_