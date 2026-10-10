// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "NewOpenProjectPanel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include <functional>
#include <utility>

#include "CollapsibleGroupBox.h"
#include "ColorSchemeManager.h"
#include "RecentProjects.h"

namespace {
// One entry of the recent projects list: the project name and, below it,
// its folder in smaller grey text. The whole entry is highlighted when
// hovered or focused and opens the project on click, Enter or Space.
class RecentProjectEntry : public QWidget {
 public:
  RecentProjectEntry(const QString& filePath, const QFont& baseFont, std::function<void()> activate, QWidget* parent)
      : QWidget(parent), m_activate(std::move(activate)) {
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QDir::toNativeSeparators(filePath));

    const QFileInfo fileInfo(filePath);
    QString baseName(fileInfo.completeBaseName());
    if (baseName.isEmpty()) {
      baseName = QChar('_');
    }

    auto* nameLabel = new QLabel(baseName, this);
    nameLabel->setTextFormat(Qt::PlainText);
    // The name one point larger than the program's text and bold, the folder in its normal size.
    QFont nameFont(baseFont);
    nameFont.setPointSize(QApplication::font().pointSize() + 1);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);

    auto* dirLabel = new QLabel(this);
    dirLabel->setTextFormat(Qt::PlainText);
    QFont dirFont(baseFont);
    dirFont.setPointSize(QApplication::font().pointSize());
    dirLabel->setFont(dirFont);
    const QFontMetrics dirMetrics(dirFont);
    dirLabel->setText(dirMetrics.elidedText(QDir::toNativeSeparators(fileInfo.absolutePath()), Qt::ElideMiddle,
                                            dirMetrics.averageCharWidth() * 70));
    // The text colour, slightly faded towards the background: readable in every colour
    // scheme. Mixed to an opaque colour, as translucent text gets fringes with ClearType.
    const QColor text(palette().color(QPalette::WindowText));
    const QColor back(palette().color(QPalette::Window));
    const QColor faded((text.red() * 3 + back.red()) / 4, (text.green() * 3 + back.green()) / 4,
                       (text.blue() * 3 + back.blue()) / 4);
    dirLabel->setStyleSheet(QString("color: %1;").arg(faded.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(0);
    layout->addWidget(nameLabel);
    layout->addWidget(dirLabel);
  }

 protected:
  void paintEvent(QPaintEvent*) override {
    if (!underMouse() && !hasFocus()) {
      return;
    }
    // A neutral grey in every colour scheme, unlike the native highlight colour.
    QColor color(palette().color(QPalette::WindowText));
    color.setAlpha(40);
    QPainter painter(this);
    painter.fillRect(rect(), color);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPoint pos = event->position().toPoint();
#else
    const QPoint pos = event->pos();
#endif
    if ((event->button() == Qt::LeftButton) && rect().contains(pos)) {
      m_activate();
    }
  }

  void keyPressEvent(QKeyEvent* event) override {
    switch (event->key()) {
      case Qt::Key_Return:
      case Qt::Key_Enter:
      case Qt::Key_Space:
        m_activate();
        break;
      default:
        QWidget::keyPressEvent(event);
    }
  }

 private:
  std::function<void()> m_activate;
};
}  // namespace

NewOpenProjectPanel::NewOpenProjectPanel(QWidget* parent) : QWidget(parent) {
  setupUi(this);

  // Bold like the panel titles of the steps.
  CollapsibleGroupBox::makeTitleBold(recentProjectsGroup);
  recentProjectsGroup->layout()->setSpacing(2);

  RecentProjects rp;
  rp.read();
  if (!rp.validate()) {
    // Some project files weren't found.
    // Write the list without them.
    rp.write();
  }
  if (rp.isEmpty()) {
    recentProjectsGroup->setVisible(false);
  } else {
    rp.enumerate([this](const QString& filePath) { addRecentProject(filePath); });
  }

  connect(newProjectButton, SIGNAL(clicked()), this, SIGNAL(newProject()));
  connect(openProjectButton, SIGNAL(clicked()), this, SIGNAL(openProject()));
}

void NewOpenProjectPanel::addRecentProject(const QString& filePath) {
  auto* entry = new RecentProjectEntry(
      filePath, recentProjectsGroup->font(), [this, filePath]() { emit openRecentProject(filePath); },
      recentProjectsGroup);
  recentProjectsGroup->layout()->addWidget(entry);
}

void NewOpenProjectPanel::paintEvent(QPaintEvent*) {
  // In fact Qt doesn't draw QWidget's background, unless
  // autoFillBackground property is set, so we can safely
  // draw our borders and shadows in the margins area.

  int left = 0, top = 0, right = 0, bottom = 0;
  layout()->getContentsMargins(&left, &top, &right, &bottom);

  const QRect widgetRect(rect());
  const QRect exceptMargins(widgetRect.adjusted(left, top, -right, -bottom));

  const int border = 1;  // Solid line border width.

  QPainter painter(this);

  // The panel sits on the image area, which is mid grey in the light scheme. Dark text with
  // ClearType fringes is hard to read there, so the panel gets the window background.
  painter.fillRect(exceptMargins, palette().window());

  const QBrush borderBrush
      = ColorSchemeManager::instance().getColorParam("OpenNewProjectBorder", palette().windowText());
  painter.setPen(QPen(borderBrush, border));

  painter.drawRect(exceptMargins);
}  // NewOpenProjectPanel::paintEvent
