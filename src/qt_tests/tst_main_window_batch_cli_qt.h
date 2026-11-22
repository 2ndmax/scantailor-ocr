// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_MAIN_WINDOW_BATCH_CLI_QT_H
#define TST_MAIN_WINDOW_BATCH_CLI_QT_H

#include <QObject>

/**
 * Compares the MainWindow batch processing path with the command line tool path
 * (CommandLineRun -> StageSequence::processAll) on the same generated project.
 */
class TestMainWindowBatchCliQt : public QObject {
  Q_OBJECT
 private slots:
  void testGuiBatchMatchesCli();
};

#endif  // TST_MAIN_WINDOW_BATCH_CLI_QT_H
