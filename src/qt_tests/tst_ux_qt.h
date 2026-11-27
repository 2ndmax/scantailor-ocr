// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_UX_QT_H
#define TST_UX_QT_H

#include <QObject>

class TestUxQt : public QObject {
  Q_OBJECT
 private slots:
  void testCollapsibleGroupBoxStateAndSignal();
  void testCollapsedStateIsLoadedForGroupDisabledAtStart();
  void testRemovedChildIsForgottenByCollapsedGroup();
  void testErrorWidgetShowsItsText();
  void testStyledIconFollowsPaletteColor();
  void testThumbnailDoesNotKeepThePreviousOne();
  void testThumbnailBorderIsTheAccentColor();
  void testColorSchemesSetTheSameParameters();
  void testTextOnAccentHasTheHigherContrast();
  void testNativeSchemeFollowsTheSameMeanings();
  void testFocusFrameOnlyForKeyboardFocus();
  void testWheelGoesPastControlsThatWereNotClicked();
  void testWheelStaysWithAClickedControl();
  void testWheelStaysWithAClickedControlAfterAnotherWindow();
  void testWheelGoesPastAControlFocusedByOpeningAWindow();
  void testWheelGoesPastAControlThatGotTheFocusOfAHiddenOne();
  void testWheelGoesPastAControlClickedWithoutTakingTheFocus();
  void testImageViewBaseEmptyAndZeroWidthGuards();
  void testBubbleAnimationDegenerateInputs();
  void testWheelPositionMapping();
  void testColorSchemeRoundedCornersAndTabbedView();
  void testDebugImageViewMissingFileShowsErrorWidget();
  void testPageSelectionAccessorNullProvider();
  void testStyledIconPackDegenerateAndHiDpiPixmap();
  void testOverlayStyleHiDpiHandlePixmap();
  void testProcessingIndicationPartialRepaintAndHideStopsTimer();
  void testThumbnailEmptyAreaAndClipPreservation();
  void testStageListViewSafeWithoutStages();
  void testZoomHandlerShortcutStack();
  void testInteractionHandlerSelfAndSiblingDeletionInDispatch();
  void testInteractiveXSplineDegenerateAndDraggableDefaults();
  void testZoneInteractionsFixes();
};

#endif
