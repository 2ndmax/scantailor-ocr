// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_OBLIQUE_COMPARE_TESTPAGE_H_
#define SCANTAILOR_TOOLS_OBLIQUE_COMPARE_TESTPAGE_H_

#include <QFont>
#include <QImage>
#include <QPointF>
#include <QString>
#include <cstdint>

namespace compare {

enum class TextAmount { FULL_PAGE, HALF_PAGE, FEW_LINES };

enum class Layout { ONE_COLUMN, TWO_COLUMNS, TEXT_WITH_PICTURE };

/** How the lines of a paragraph are set. Only justified lines have straight edges on both sides. */
enum class Alignment { JUSTIFIED, RAGGED_RIGHT, CENTERED };

enum class Disturbance { NONE, NOISE, BLUR };

/** None checks for false alarms; small is the slight shear a user notices only on a close look. */
enum class ShearClass { NONE, SMALL, LARGE };

QString toString(TextAmount text);
QString toString(Layout layout);
QString toString(Alignment alignment);
QString toString(Disturbance disturbance);
QString toString(ShearClass shearClass);

/** What a test page looks like. The same index and seed always give the same page. */
struct PageSpec {
  int index = 0;
  /** The deskew angle that straightens the scan, in the sense of deskew::Task::findDeskewAngle(). */
  double rotationDeg = 0;
  /** The oblique angle that straightens the scan, in the sense of deskew::Task::findObliqueAngle(). */
  double shearDeg = 0;
  ShearClass shearClass = ShearClass::NONE;
  /**
   * Perspective, as from a camera tilted towards the top or bottom: the bottom of the page is this
   * much wider than the top, relative to the page width (negative: narrower). The left and right
   * edges then lean in opposite directions; it is no shear, and the page has none besides.
   */
  double keystone = 0;
  TextAmount text = TextAmount::FULL_PAGE;
  Layout layout = Layout::ONE_COLUMN;
  Alignment alignment = Alignment::JUSTIFIED;
  int dpi = 300;
  Disturbance disturbance = Disturbance::NONE;
};

/** Text amount, layout, alignment and resolution go round in turn: every combination once in 81 pages. */
const int PAGES_PER_ROUND = 81;

/**
 * The fixed sample. The angles are random and not on any grid: rotation within ±5°, shear 0° on
 * one page in six, 0.1° to 1° on two in six, 1° to 4° on three in six, either sign. Half of the
 * pages without shear get a keystone of 1 % to 6 % instead.
 */
PageSpec pageSpec(int index, uint32_t seed);

/**
 * The font for the text: Arial, which every Windows has, or else the sans serif font Qt finds.
 * The family really used is written into the report.
 */
QFont textFont();

/** The upright page without disturbance (8 bits with a gray palette, as imageproc expects). */
QImage renderPage(const PageSpec& spec, uint32_t seed, const QFont& font);

/**
 * The page as a scanner would deliver it: sheared, rotated, moved by \p shift pixels and disturbed.
 * A shift by a fraction of a pixel changes nothing but the sampling grid.
 */
QImage scannedPage(const QImage& upright, const PageSpec& spec, uint32_t seed, const QPointF& shift = QPointF());

}  // namespace compare

#endif
