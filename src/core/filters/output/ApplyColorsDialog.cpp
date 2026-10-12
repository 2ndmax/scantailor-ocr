// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplyColorsDialog.h"

#include <QPushButton>

#include "PageSelectionAccessor.h"

namespace output {
ApplyColorsDialog::ApplyColorsDialog(QWidget* parent,
                                     const PageId& curPage,
                                     const PageSelectionAccessor& pageSelectionAccessor)
    : QDialog(parent), m_curPage(curPage) {
  setupUi(this);
  scopeGroupBox->setPages(curPage, pageSelectionAccessor);
  optionsBox->setVisible(false);

  // Applying nothing makes no sense.
  const auto updateOkButton = [this] {
    buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(applyColorModeOption->isChecked() || applyOtherSettingsOption->isChecked());
  };
  connect(applyColorModeOption, &QAbstractButton::toggled, this, updateOkButton);
  connect(applyOtherSettingsOption, &QAbstractButton::toggled, this, updateOkButton);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &ApplyColorsDialog::onSubmit);
}

ApplyColorsDialog::~ApplyColorsDialog() = default;

void ApplyColorsDialog::showModeParts() {
  m_modeParts = true;
  optionsBox->setVisible(true);
}

bool ApplyColorsDialog::isColorModeChosen() const {
  return !m_modeParts || applyColorModeOption->isChecked();
}

bool ApplyColorsDialog::isOtherSettingsChosen() const {
  return !m_modeParts || applyOtherSettingsOption->isChecked();
}

void ApplyColorsDialog::setThisPageIncluded(const bool included) {
  m_thisPageIncluded = included;
}

void ApplyColorsDialog::onSubmit() {
  if (scopeGroupBox->isThisPageOnly()) {
    // Usually not handled: the options panel has already applied it.
    emit accepted(m_thisPageIncluded ? std::set<PageId>{m_curPage} : std::set<PageId>());
  } else {
    emit accepted(scopeGroupBox->pages());
  }

  // We assume the default connection from accepted() to accept()
  // was removed.
  accept();
}
}  // namespace output
