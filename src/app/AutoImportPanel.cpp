// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AutoImportPanel.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "CollapsibleGroupBox.h"

AutoImportPanel::AutoImportPanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  // The options of the step follow right below, with their own margin.
  layout->setContentsMargins(layout->contentsMargins().left(), layout->contentsMargins().top(),
                             layout->contentsMargins().right(), 0);

  // Collapsible like the other panels; the object name keeps its collapsed state.
  auto* group = new CollapsibleGroupBox(tr("Automatic import"));
  group->setObjectName("autoImportPanel");
  auto* groupLayout = new QVBoxLayout(group);
  layout->addWidget(group);

  m_importCheck = new QCheckBox(tr("Import new scans"));
  groupLayout->addWidget(m_importCheck);
  setImportPossible(true);

  // The label on a line of its own leaves the path the width next to the button.
  groupLayout->addWidget(new QLabel(tr("Folder:")));
  auto* dirLayout = new QHBoxLayout;
  m_dirLabel = new QLabel;
  // Long paths are shortened at the front instead of widening the panel.
  m_dirLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_changeDirBtn = new QPushButton(tr("Change ..."));
  m_changeDirBtn->setToolTip(tr("Choose the folder the scanning program saves its images to."));
  m_changeDirBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  dirLayout->addWidget(m_dirLabel, 1);
  dirLayout->addWidget(m_changeDirBtn);
  groupLayout->addLayout(dirLayout);

  m_appendMode = new QRadioButton(tr("Add at the end"));
  m_appendMode->setToolTip(tr("Each new scan becomes the last page."));
  m_insertAfterMode = new QRadioButton(tr("Insert after selected page"));
  m_insertAfterMode->setToolTip(
      tr("New scans follow the page selected now, one after the other, and are named after it, so they are "
         "also sorted after it in the folder.  Selecting another page continues there."));
  m_replaceMode = new QRadioButton(tr("Replace selected page with next scan"));
  m_replaceMode->setToolTip(tr("Not available yet."));
  m_modes = new QButtonGroup(this);
  m_modes->addButton(m_appendMode, APPEND);
  m_modes->addButton(m_insertAfterMode, INSERT_AFTER);
  m_modes->addButton(m_replaceMode, REPLACE);
  for (QRadioButton* mode : {m_appendMode, m_insertAfterMode, m_replaceMode}) {
    groupLayout->addWidget(mode);
  }
  m_appendMode->setChecked(true);

  m_noDpiLabel = new QLabel;
  m_noDpiLabel->setWordWrap(true);
  m_noDpiLabel->setToolTip(tr("Set the DPI with Tools > Fix DPI ..."));
  m_noDpiLabel->hide();
  groupLayout->addWidget(m_noDpiLabel);

  connect(m_importCheck, &QCheckBox::toggled, this, [this](const bool checked) {
    updateEnabled();
    emit importToggled(checked);
  });
  connect(m_changeDirBtn, &QPushButton::clicked, this, &AutoImportPanel::changeDirectoryRequested);
  for (QAbstractButton* button : m_modes->buttons()) {
    connect(button, &QAbstractButton::toggled, this, [this, button](const bool checked) {
      if (checked && !m_settingMode) {
        emit modeChanged(static_cast<Mode>(m_modes->id(button)));
      }
    });
  }

  updateEnabled();
}

void AutoImportPanel::setMode(const Mode mode) {
  m_settingMode = true;
  m_modes->button(mode)->setChecked(true);
  m_settingMode = false;
}

AutoImportPanel::Mode AutoImportPanel::mode() const {
  return static_cast<Mode>(m_modes->checkedId());
}

void AutoImportPanel::setInsertingPossible(const bool possible) {
  m_insertingPossible = possible;
  updateEnabled();
}

AutoImportPanel::~AutoImportPanel() = default;

void AutoImportPanel::setImporting(const bool importing) {
  const QSignalBlocker blocker(m_importCheck);
  m_importCheck->setChecked(importing);
  updateEnabled();
}

bool AutoImportPanel::isImporting() const {
  return m_importCheck->isChecked();
}

void AutoImportPanel::setImportPossible(const bool possible) {
  m_importCheck->setEnabled(possible);
  m_importCheck->setToolTip(possible ? tr("New images saved to the folder below are added to the project "
                                          "automatically.  Images that are already there when this is switched "
                                          "on can be chosen once.")
                                     : tr("Save the project first."));
  if (!possible) {
    setImporting(false);
  }
}

void AutoImportPanel::setDirectory(const QString& dir) {
  m_dir = dir;
  m_dirLabel->setToolTip(QDir::toNativeSeparators(dir));
  updateDirectoryLabel();
}

void AutoImportPanel::setScansWithoutDpi(const int count) {
  m_noDpiLabel->setText(tr("%n scan(s) without DPI.", "", count));
  m_noDpiLabel->setVisible(count > 0);
}

void AutoImportPanel::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  updateDirectoryLabel();
}

void AutoImportPanel::updateDirectoryLabel() {
  // Shortened at the front, so the end of the path stays visible.
  const QString path = QDir::toNativeSeparators(m_dir);
  const int width = m_dirLabel->width();
  m_dirLabel->setText((width > 0) ? m_dirLabel->fontMetrics().elidedText(path, Qt::ElideLeft, width) : path);
}

void AutoImportPanel::updateEnabled() {
  const bool importing = m_importCheck->isChecked();
  m_appendMode->setEnabled(importing);
  // Inserting needs a page to insert after.
  m_insertAfterMode->setEnabled(importing && m_insertingPossible);
  // Follows in the next part.
  m_replaceMode->setEnabled(false);
}
