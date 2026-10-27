// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_ux_qt.h"

#include <CollapsibleGroupBox.h>
#include <ErrorWidget.h>
#include <StyledIconPack.h>

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <algorithm>

void TestUxQt::testCollapsibleGroupBoxStateAndSignal() {
  CollapsibleGroupBox group("Options");
  QSignalSpy spy(&group, &CollapsibleGroupBox::collapsedStateChanged);
  QVERIFY(spy.isValid());

  QVERIFY(!group.isCollapsed());

  group.setCollapsed(true);
  QVERIFY(group.isCollapsed());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toBool(), true);

  group.setCollapsed(false);
  QVERIFY(!group.isCollapsed());
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TestUxQt::testCollapsedStateIsLoadedForGroupDisabledAtStart() {
  // A group that is disabled when it is first shown (e.g. the picture options on a black and white page)
  // must still get the state the user left it in, or the next start overwrites that state.
  const QString key = "CollapsibleGroupBox/disabledAtStart";
  QSettings().setValue(key + "/collapsed", true);
  {
    CollapsibleGroupBox group("Options");
    group.setObjectName("disabledAtStart");
    group.setEnabled(false);
    group.show();
    QVERIFY(group.isCollapsed());
  }
  QSettings().remove(key);
}

void TestUxQt::testErrorWidgetShowsItsText() {
  // The text and its format reach the label of the widget.
  const QString text("<b>Test error</b>");
  ErrorWidget widget(text, Qt::RichText);
  const QList<QLabel*> labels = widget.findChildren<QLabel*>();
  const auto textLabel
      = std::find_if(labels.begin(), labels.end(), [&](const QLabel* label) { return label->text() == text; });
  QVERIFY(textLabel != labels.end());
  QCOMPARE((*textLabel)->textFormat(), Qt::RichText);
}

void TestUxQt::testStyledIconFollowsPaletteColor() {
  // Styled icons are colored with the palette's text color and kept for the next repaint; a new
  // color scheme must still recolor them.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QFile svg(dir.filePath("square.svg"));
  QVERIFY(svg.open(QIODevice::WriteOnly));
  svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\"/></svg>");
  svg.close();
  QFile sheet(dir.filePath("icons.xml"));
  QVERIFY(sheet.open(QIODevice::WriteOnly));
  sheet.write(
      ("<icons><icon name=\"square\" styled=\"on\"><file>" + svg.fileName() + "</file></icon></icons>").toUtf8());
  sheet.close();

  StyledIconPack pack(sheet.fileName());
  const QIcon icon = pack.getIcon("square");
  const auto centerColor = [&]() { return icon.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8); };

  const QPalette oldPalette = QGuiApplication::palette();
  QPalette palette(oldPalette);
  palette.setColor(QPalette::Normal, QPalette::WindowText, QColor(0x20, 0x80, 0x40));
  QGuiApplication::setPalette(palette);
  const QColor first = centerColor();
  palette.setColor(QPalette::Normal, QPalette::WindowText, QColor(0xa0, 0x10, 0x10));
  QGuiApplication::setPalette(palette);
  const QColor second = centerColor();
  QGuiApplication::setPalette(oldPalette);

  QCOMPARE(first, QColor(0x20, 0x80, 0x40));
  QCOMPARE(second, QColor(0xa0, 0x10, 0x10));
}
