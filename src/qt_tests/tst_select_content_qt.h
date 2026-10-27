// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_SELECT_CONTENT_QT_H
#define TST_SELECT_CONTENT_QT_H

#include <QObject>

class TestSelectContentQt : public QObject {
  Q_OBJECT
 private slots:
  void testChangedDetectionOptionsDetectAgain();
  void testOldPagesDetectAgainOnlyWithOtherOptions();
};

#endif
