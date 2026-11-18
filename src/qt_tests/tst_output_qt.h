// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_OUTPUT_QT_H
#define TST_OUTPUT_QT_H

#include <QObject>

class TestOutputQt : public QObject {
  Q_OBJECT
 private slots:
  void testShowsPanelAndConnectedTabs();
  void testMixedModeShowsPictureZones();
  void testThumbnailOfPage();
};

#endif  // TST_OUTPUT_QT_H
