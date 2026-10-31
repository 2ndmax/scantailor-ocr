// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_END_TO_END_QT_H
#define TST_END_TO_END_QT_H

#include <QObject>

/**
 * Creates a project from synthetic scans, runs all six steps as the app does and checks
 * properties of the result. The settings of the computer (profile, QSettings) play no role.
 */
class TestEndToEndQt : public QObject {
  Q_OBJECT
 private slots:
  void testAllSteps();
  void testColorOutputCompression();
  void testMissingImage();
  void testMissingImageAfterEarlierRun();
  void testOutputTooLarge();
  void testCancelWithoutThumbnails();
  void testPdfExport();
};

#endif
