// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrientationDetectionDialog.h"

#include <QPointer>

#include "PageSelectionAccessor.h"

namespace fix_orientation {
OrientationDetectionDialog::OrientationDetectionDialog(QWidget* parent,
                                                       const PageId& curPage,
                                                       const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent), m_curPage(curPage) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &OrientationDetectionDialog::onSubmit);
}

OrientationDetectionDialog::~OrientationDetectionDialog() = default;

void OrientationDetectionDialog::onSubmit() {
  const bool bookRule = bookRuleCheckBox->isChecked();
  const std::set<PageId> pages = scopeGroupBox->isThisPageOnly() ? std::set<PageId>{m_curPage} : scopeGroupBox->pages();

  // Hide before emitting so synchronous progress/summary dialogs do not leave this modal dialog visible underneath.
  QPointer<OrientationDetectionDialog> self(this);
  hide();
  emit detectionRequested(pages, bookRule);

  // We assume the default connection from accepted() to accept() was removed.
  if (self) {
    accept();
  }
}
}  // namespace fix_orientation