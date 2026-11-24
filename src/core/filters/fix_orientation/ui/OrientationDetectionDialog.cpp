// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OrientationDetectionDialog.h"

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

void OrientationDetectionDialog::setThisPageIncluded(const bool included) {
  m_thisPageIncluded = included;
}

void OrientationDetectionDialog::onSubmit() {
  const bool bookRule = bookRuleCheckBox->isChecked();
  if (scopeGroupBox->isThisPageOnly()) {
    emit accepted(m_thisPageIncluded ? std::set<PageId>{m_curPage} : std::set<PageId>(), bookRule);
  } else {
    emit accepted(scopeGroupBox->pages(), bookRule);
  }

  // We assume the default connection from accepted() to accept() was removed.
  accept();
}
}  // namespace fix_orientation