// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_core_qt.h"
#include "tst_deskew_qt.h"
#include "tst_fix_orientation_qt.h"
#include "tst_ux_qt.h"

#include <QApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // Keep the tests away from persistent state: the settings (ApplicationSettings) go to an INI
  // file in a temporary folder, the standard paths (e.g. the profile folder) to Qt's test folders.
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir settingsDir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

  int status = 0;

  TestCoreQt tc1;
  status |= QTest::qExec(&tc1, argc, argv);

  TestUxQt tc2;
  status |= QTest::qExec(&tc2, argc, argv);

  TestDeskewQt tc3;
  status |= QTest::qExec(&tc3, argc, argv);
  TestFixOrientationQt tc4;
  status |= QTest::qExec(&tc4, argc, argv);

  return status;
}
