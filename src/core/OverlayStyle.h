// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OVERLAYSTYLE_H_
#define SCANTAILOR_CORE_OVERLAYSTYLE_H_

#include <QColor>
#include <QPen>
#include <QPixmap>

/**
 * The colors and line widths of what the image views and the thumbnails draw over the pages:
 * grids, arcs, split lines, boxes, margins, zones and handles. They lie on the scanned pages,
 * mostly white, so they don't follow the color scheme or the accent color.
 */
namespace OverlayStyle {
/** How strong a line is drawn. Each place picks the level of its role, not a width of its own. */
enum class Line {
  /** Grids and orientation lines over the text, resting zone outlines: 2 pixels. */
  Fine,
  /** Editable borders on the page (split line, boxes, margins, guides), a zone being edited: 3 pixels. */
  Normal,
  /** Controls at or beyond the edge of the page, such as the rotation arcs: 4 pixels. */
  Strong,
  /** All lines in thumbnails, which are too small for the levels above: 1 pixel. */
  Thumbnail
};

/** The width of a line level in screen pixels. */
qreal lineWidth(Line line);

/** A cosmetic pen (same width at any zoom) of the given color and line level. */
QPen pen(const QColor& color, Line line);

/** Lines, boxes and tints of the main element: grids, arcs, split lines, content box, margins. */
QColor primary(int alpha = 255);

/** The second element where two must be told apart: page box, right page, aligned margins, guides. */
QColor secondary(int alpha = 255);

/** A page that is removed. */
QColor removed();

/** A handle to drag: a white circle with a border in the primary color, 16 pixels wide. */
QPixmap handlePixmap();
}  // namespace OverlayStyle

#endif  // SCANTAILOR_CORE_OVERLAYSTYLE_H_
