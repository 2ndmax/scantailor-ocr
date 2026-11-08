// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_PAGE_LAYOUT_QT_H
#define TST_PAGE_LAYOUT_QT_H

#include <QObject>

class TestPageLayoutQt : public QObject {
  Q_OBJECT
 private slots:
  void testLengthFieldsSetDecimalsStepAndUnit();
};

#endif  // TST_PAGE_LAYOUT_QT_H
