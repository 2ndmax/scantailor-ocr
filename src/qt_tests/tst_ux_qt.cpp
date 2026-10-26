// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_ux_qt.h"

#include <CollapsibleGroupBox.h>
#include <ErrorWidget.h>

#include <QLabel>
#include <QList>
#include <QSettings>
#include <QSignalSpy>
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
