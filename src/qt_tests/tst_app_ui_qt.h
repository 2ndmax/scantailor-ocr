// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_APP_UI_QT_H
#define TST_APP_UI_QT_H

#include <QObject>

class TestAppUiQt : public QObject {
  Q_OBJECT
 private slots:
  void testStateIconCreatesAllColors();
  void testFixDpiTreeModelIndexRejectsInvalidIndices();
  void testThumbnailSequenceDuplicateInsertAndSingleColumnCentering();
  void testThumbnailSequenceSurvivesRemovalDuringSelectionQuestion();
  void testExportViewClearsStatusOnRowRemoval();
  void testProjectFilesDialogSanitizesPathsStopsTimersAndChecksOutDirFirst();
  void testMainWindowHidesRemovedKeepOwnershipWidgetsAndGuardsAutoImport();
};

#endif  // TST_APP_UI_QT_H
