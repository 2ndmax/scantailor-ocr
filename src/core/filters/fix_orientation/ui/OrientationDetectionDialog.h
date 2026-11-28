// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

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

 signals:
  void detectionRequested(const std::set<PageId>& pages, bool bookRule);

 private slots:
  void onSubmit();

 private:
  PageId m_curPage;
};
}  // namespace fix_orientation

#endif  // SCANTAILOR_FIX_ORIENTATION_ORIENTATIONDETECTIONDIALOG_H_