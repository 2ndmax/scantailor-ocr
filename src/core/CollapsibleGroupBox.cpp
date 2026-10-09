// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CollapsibleGroupBox.h"

#include <foundation/ScopedIncDec.h>

#include <QApplication>
#include <QSettings>
#include <QtCore/QEvent>
#include <QtGui/QShowEvent>
#include <QtWidgets/QStyleOptionGroupBox>
#include <QtWidgets/QStylePainter>

#include "IconProvider.h"

namespace {
// Paints a group box like QGroupBox::paintEvent(), but the title with a bold font.  Setting a
// bold font on the group box itself would make all of its content bold as well.
class BoldTitlePainter : public QObject {
 public:
  using QObject::QObject;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override {
    auto* groupBox = qobject_cast<QGroupBox*>(watched);
    if (!groupBox || (event->type() != QEvent::Paint)) {
      return false;
    }

    QFont font = groupBox->font();
    font.setBold(true);

    QStyleOptionGroupBox option;
    option.initFrom(groupBox);
    option.fontMetrics = QFontMetrics(font);
    option.text = groupBox->title();
    option.lineWidth = 1;
    option.midLineWidth = 0;
    option.textAlignment = groupBox->alignment();
    option.subControls = QStyle::SC_GroupBoxFrame;
    if (groupBox->isFlat()) {
      option.features |= QStyleOptionFrame::Flat;
    }
    if (groupBox->isCheckable()) {
      option.subControls |= QStyle::SC_GroupBoxCheckBox;
      option.state |= groupBox->isChecked() ? QStyle::State_On : QStyle::State_Off;
    }
    if (!option.palette.isBrushSet(QPalette::Current, QPalette::WindowText)) {
      option.textColor = QColor(groupBox->style()->styleHint(QStyle::SH_GroupBox_TextLabelColor, &option, groupBox));
    }
    if (!option.text.isEmpty()) {
      option.subControls |= QStyle::SC_GroupBoxLabel;
    }

    QStylePainter painter(groupBox);
    painter.setFont(font);
    painter.drawComplexControl(QStyle::CC_GroupBox, option);
    return true;
  }
};
}  // namespace

CollapsibleGroupBox::CollapsibleGroupBox(QWidget* parent) : QGroupBox(parent) {
  initialize();
}

CollapsibleGroupBox::CollapsibleGroupBox(const QString& title, QWidget* parent) : QGroupBox(title, parent) {
  initialize();
}

void CollapsibleGroupBox::initialize() {
  m_collapseIcon = IconProvider::getInstance().getIcon("collapse");
  m_expandIcon = IconProvider::getInstance().getIcon("expand");
  m_collapseButton = new QToolButton(this);
  m_collapseButton->setObjectName("collapseButton");
  m_collapseButton->setAutoRaise(true);
  m_collapseButton->setFixedSize(14, 14);
  m_collapseButton->setIconSize({10, 10});
  m_collapseButton->setIcon(m_collapseIcon);
  setFocusProxy(m_collapseButton);
  setFocusPolicy(Qt::StrongFocus);

  this->setAlignment(Qt::AlignCenter);
  makeTitleBold(this);

  connect(m_collapseButton, &QAbstractButton::clicked, this, &CollapsibleGroupBox::toggleCollapsed);
  connect(this, &QGroupBox::toggled, this, &CollapsibleGroupBox::checkToggled);
  connect(this, &QGroupBox::clicked, this, &CollapsibleGroupBox::checkClicked);
}

void CollapsibleGroupBox::setCollapsed(const bool collapse) {
  const bool changed = (collapse != m_collapsed);

  if (changed) {
    m_collapsed = collapse;
    m_collapseButton->setIcon(collapse ? m_expandIcon : m_collapseIcon);

    updateWidgets();

    emit collapsedStateChanged(isCollapsed());
  }
}

void CollapsibleGroupBox::makeTitleBold(QGroupBox* groupBox) {
  static BoldTitlePainter* const painter = new BoldTitlePainter(qApp);
  groupBox->installEventFilter(painter);
}

bool CollapsibleGroupBox::isCollapsed() const {
  return m_collapsed;
}

void CollapsibleGroupBox::checkToggled(bool) {
  m_collapseButton->setEnabled(true);
}

void CollapsibleGroupBox::checkClicked(bool checked) {
  if (checked && isCollapsed()) {
    setCollapsed(false);
  } else if (!checked && !isCollapsed()) {
    setCollapsed(true);
  }
}

void CollapsibleGroupBox::toggleCollapsed() {
  // verify if sender is this group box's collapse button
  auto* sender = dynamic_cast<QToolButton*>(QObject::sender());
  const bool isSenderCollapseButton = (sender && (sender == m_collapseButton));

  if (isSenderCollapseButton) {
    setCollapsed(!isCollapsed());
  }
}

void CollapsibleGroupBox::updateWidgets() {
  const ScopedIncDec<int> guard(m_ignoreVisibilityEvents);

  if (m_collapsed) {
    for (QObject* child : children()) {
      auto* widget = dynamic_cast<QWidget*>(child);
      if (widget && (widget != m_collapseButton) && widget->isVisible()) {
        m_collapsedWidgets.insert(widget);
        widget->hide();
      }
    }
  } else {
    for (QObject* child : children()) {
      auto* widget = dynamic_cast<QWidget*>(child);
      if (widget && (widget != m_collapseButton) && (m_collapsedWidgets.find(widget) != m_collapsedWidgets.end())) {
        m_collapsedWidgets.erase(widget);
        widget->show();
      }
    }
  }
}

void CollapsibleGroupBox::showEvent(QShowEvent* event) {
  // initialize widget on first show event only
  if (m_shown) {
    event->accept();
    return;
  }
  m_shown = true;

  loadState();

  QWidget::showEvent(event);
}

void CollapsibleGroupBox::changeEvent(QEvent* event) {
  QGroupBox::changeEvent(event);

  if ((event->type() == QEvent::EnabledChange) && isEnabled()) {
    m_collapseButton->setEnabled(true);
  }
}

void CollapsibleGroupBox::childEvent(QChildEvent* event) {
  auto* childWidget = dynamic_cast<QWidget*>(event->child());
  if (childWidget && (event->type() == QEvent::ChildAdded)) {
    if (m_collapsed) {
      if (childWidget->isVisible()) {
        m_collapsedWidgets.insert(childWidget);
        childWidget->hide();
      }
    }

    childWidget->installEventFilter(this);
  }

  QGroupBox::childEvent(event);
}

bool CollapsibleGroupBox::eventFilter(QObject* watched, QEvent* event) {
  if (m_collapsed && !m_ignoreVisibilityEvents) {
    auto* childWidget = dynamic_cast<QWidget*>(watched);
    if (childWidget) {
      if (event->type() == QEvent::ShowToParent) {
        const ScopedIncDec<int> guard(m_ignoreVisibilityEvents);

        m_collapsedWidgets.insert(childWidget);
        childWidget->hide();
      } else if (event->type() == QEvent::HideToParent) {
        m_collapsedWidgets.erase(childWidget);
      }
    }
  }
  return QObject::eventFilter(watched, event);
}

CollapsibleGroupBox::~CollapsibleGroupBox() {
  saveState();
}

void CollapsibleGroupBox::loadState() {
  if (!isEnabled()) {
    return;
  }

  const QString key = getSettingsKey();
  if (key.isEmpty()) {
    return;
  }

  setUpdatesEnabled(false);

  QSettings settings;

  if (isCheckable()) {
    QVariant val = settings.value(key + "/checked");
    if (!val.isNull()) {
      setChecked(val.toBool());
    }
  }

  {
    QVariant val = settings.value(key + "/collapsed");
    if (!val.isNull()) {
      setCollapsed(val.toBool());
    }
  }

  setUpdatesEnabled(true);
}

void CollapsibleGroupBox::saveState() {
  if (!m_shown || !isEnabled()) {
    return;
  }

  const QString key = getSettingsKey();
  if (key.isEmpty()) {
    return;
  }

  QSettings settings;

  if (isCheckable()) {
    settings.setValue(key + "/checked", isChecked());
  }
  settings.setValue(key + "/collapsed", isCollapsed());
}

QString CollapsibleGroupBox::getSettingsKey() const {
  if (objectName().isEmpty()) {
    return QString();
  }

  QString saveKey = '/' + objectName();
  saveKey = "CollapsibleGroupBox" + saveKey;
  return saveKey;
}
