// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_core_qt.h"
#include "tst_deskew_qt.h"
#include "tst_end_to_end_qt.h"
#include "tst_fix_orientation_qt.h"
#include "tst_ux_qt.h"

#include <QApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <vector>

namespace {
// Options of QTest that take the next argument as their value.
const QStringList kOptionsWithValue{"-o",          "-maxwarnings", "-eventdelay", "-keydelay",     "-mousedelay",
                                    "-seed",       "-iterations",  "-median",     "-minimumvalue", "-minimumtotal",
                                    "-perfcounter"};

/** "results.txt,txt" becomes "results_TestCoreQt.txt,txt", so each class keeps its own file. */
QString outputForClass(const QString& value, const QString& className) {
  const qsizetype comma = value.lastIndexOf(',');
  const QString fileName = (comma < 0) ? value : value.left(comma);
  const QString format = (comma < 0) ? QString() : value.mid(comma);
  if (fileName == "-") {
    return value;  // The console is shared anyway.
  }
  const QFileInfo info(fileName);
  QString name = info.completeBaseName() + '_' + className;
  if (!info.suffix().isEmpty()) {
    name += '.' + info.suffix();
  }
  return QDir(info.path()).filePath(name) + format;
}

bool hasTestFunction(const QObject& testObject, const QString& selection) {
  // "testFoo:dataTag" selects one row of a data-driven test.
  const QString function = selection.section(':', 0, 0);
  return testObject.metaObject()->indexOfMethod(QMetaObject::normalizedSignature(qPrintable(function + "()"))) >= 0;
}

/**
 * Runs one test class. Each qExec() reads the command line anew, so the arguments are adapted:
 * every class writes its own output file, and gets only the test functions it knows.
 * Returns the number of failed tests; a class none of the selected functions belongs to is skipped.
 */
int runTestClass(QObject& testObject, const QStringList& arguments) {
  const QString className = testObject.metaObject()->className();
  QStringList classArguments{arguments.first()};
  bool selectionGiven = false;
  bool selectionMatches = false;
  for (qsizetype i = 1; i < arguments.size(); ++i) {
    const QString& argument = arguments[i];
    if (kOptionsWithValue.contains(argument) && (i + 1 < arguments.size())) {
      const QString& value = arguments[++i];
      classArguments << argument << ((argument == "-o") ? outputForClass(value, className) : value);
    } else if (argument.startsWith('-')) {
      classArguments << argument;
    } else {
      selectionGiven = true;
      if (hasTestFunction(testObject, argument)) {
        selectionMatches = true;
        classArguments << argument;
      }
    }
  }
  if (selectionGiven && !selectionMatches) {
    return 0;
  }
  return QTest::qExec(&testObject, classArguments);
}
}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);

  // Keep the tests away from persistent state: the settings (ApplicationSettings) go to an INI
  // file in a temporary folder, the standard paths (e.g. the profile folder) to Qt's test folders.
  QStandardPaths::setTestModeEnabled(true);
  QTemporaryDir settingsDir;
  if (!settingsDir.isValid()) {
    qCritical() << "Can't create a temporary folder for the settings:" << settingsDir.errorString();
    return 1;
  }
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

  const QStringList arguments = app.arguments();
  TestCoreQt tc1;
  TestUxQt tc2;
  TestDeskewQt tc3;
  TestFixOrientationQt tc4;
  TestEndToEndQt tc5;
  const std::vector<QObject*> testObjects{&tc1, &tc2, &tc3, &tc4, &tc5};

  int failed = 0;
  for (QObject* testObject : testObjects) {
    failed += runTestClass(*testObject, arguments);
  }
  // The exit code has only 8 bits on some systems, so a count could wrap around to 0.
  return (failed != 0) ? 1 : 0;
}
