// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_OBLIQUE_COMPARE_METHODS_H_
#define SCANTAILOR_TOOLS_OBLIQUE_COMPARE_METHODS_H_

#include <optional>

class QImage;

namespace imageproc {
class BinaryImage;
}  // namespace imageproc

// Candidate oblique methods for the comparison. Each takes the page and the deskew angle found for
// it, like deskew::Task::findObliqueAngle(), and returns the oblique angle in the same sense, or
// nothing if the page does not show it clearly enough.
//
// Up to commit 73d7c64c the tool also measured, and then dropped: the sausage mask with the full
// angle and without the confidence check (it carries no information about the shear), a projection
// search over the stroke edges (false alarms, thrown off by pictures) and a fit through the line
// starts alone (takes a keystone for a shear).
namespace compare {

/** What a method finds: an angle or nothing, and how sure it is, as the method measures that. */
struct Finding {
  std::optional<double> angle;
  std::optional<double> confidence;
};

/**
 * (c2) Fits straight lines through the starts and ends of the text lines of every column of the
 * deskewed, cleaned-up page, so also through the right edge of justified text. Lines that start
 * elsewhere (centered text, a picture beside them) are left out as outliers. All edges found must
 * lean alike within EDGES_AGREE_DEG; if they do not, it is no shear (perspective, a curved page)
 * and there is no angle. The confidence is the number of edges, negative if they disagree.
 */
Finding edgeFit(const imageproc::BinaryImage& image, double deskewAngleDeg);

/**
 * (d) The direction of the vertical stroke edges on the gray page (stems of letters), counting only
 * edges that run straight for a third of a millimetre: the most frequent direction, refined by
 * least squares in narrowing windows around it. Needs no straight edge of the text block. On a page
 * set in italics the most frequent direction is their slant; a second direction of similar weight
 * (italics among upright type) gives no angle. The confidence is the height of that second
 * direction relative to the first. Measured limit: whole pages in italics still get a wrong angle
 * now and then; Times New Roman reads about 0.1° too far.
 */
Finding strokeDirection(const QImage& gray, double deskewAngleDeg);

/**
 * (e) (c2) where the text has straight edges, else (d). Edges that disagree give no angle. A single
 * edge (ragged text in one column) cannot tell a shear from a keystone, so (d) has to confirm it
 * within EDGE_STROKES_AGREE_DEG. Without an edge, (d) counts only up to STROKE_FALLBACK_MAX_DEG.
 */
Finding edgesThenStrokes(const imageproc::BinaryImage& image, const QImage& gray, double deskewAngleDeg);

}  // namespace compare

#endif
