// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OVERLAYSTYLE_H_
#define SCANTAILOR_CORE_OVERLAYSTYLE_H_

#include <QColor>
#include <QPixmap>

/**
 * The colors of what the image views and the thumbnails draw over the pages: grids, arcs, split
 * lines, boxes, margins, zones and handles. They lie on the scanned pages, mostly white, so they
 * don't follow the color scheme or the accent color.
 */
namespace OverlayStyle {
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
