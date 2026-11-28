// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ErrorWidget.h"

#include <QApplication>
#include <QIcon>
#include <QStyle>

#include "ui_ErrorWidget.h"

ErrorWidget::ErrorWidget(const QString& text, Qt::TextFormat fmt) : ui(std::make_unique<Ui::ErrorWidget>()) {
  ui->setupUi(this);
  ui->textLabel->setTextFormat(fmt);
  ui->textLabel->setText(text);
  if (QStyle* style = QApplication::style()) {
    const QIcon icon(style->standardIcon(QStyle::SP_MessageBoxWarning));
    ui->imageLabel->setPixmap(icon.pixmap(QSize(48, 48), devicePixelRatioF()));
  }

  connect(ui->textLabel, &QLabel::linkActivated, this, &ErrorWidget::linkActivated);
}

ErrorWidget::~ErrorWidget() = default;
