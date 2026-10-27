// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPL v3 license that can be found in the LICENSE file.
//
// Auto oblique (Issue #2), requested by Zvezdochiot. Up to 2026-10 the angle came from a "sausage"
// mask turned by 90 degrees (PR #110); on ordinary text it almost never found an angle, so it was
// replaced by the straight edges of the text (compared with the developer tool oblique_compare).

#ifndef SCANTAILOR_CORE_OBLIQUEFINDER_H_
#define SCANTAILOR_CORE_OBLIQUEFINDER_H_

#include <optional>

class TaskStatus;

namespace imageproc {
class BinaryImage;
}  // namespace imageproc

namespace deskew {

/**
 * \brief The mask rotated by \p deskewAngleDeg, like ImageTransformation rotates the page.
 *
 * With the deskew angle found for the mask, its lines become horizontal, as measureTextEdges()
 * expects.
 */
imageproc::BinaryImage deskewedMask(const imageproc::BinaryImage& mask, double deskewAngleDeg);

/** What the straight vertical edges of the text tell about the oblique angle of a page. */
struct TextEdges {
  /** The straight edges found: where many lines of a column start or end. */
  int count = 0;
  /**
   * Whether all edges lean alike. If they do not, the page is not sheared but seen in perspective
   * or curved, and shearing it would set one edge right and another one wrong.
   */
  bool agree = false;
  /** The weighted mean lean of the edges, as the oblique angle that straightens the page; 0 without edges. */
  double angleDeg = 0;
};

/**
 * \brief Finds the straight vertical edges of the text on a deskewed mask (see deskewedMask()).
 *
 * For each line of text, every column contributes where its text starts and ends. Straight lines
 * through many of these points, robust against lines that start elsewhere (indents, a picture
 * beside them, centered text), are the edges: the left edge of every column and the right edge of
 * justified text. A few lines are too short an edge; one pixel off at a line start tilts it by a lot.
 *
 * \param status Cancels the search, which takes seconds on a page with a large table.
 * \param resolutionRatio Horizontal by vertical resolution; the angle is a physical one.
 */
TextEdges measureTextEdges(const imageproc::BinaryImage& deskewedMask,
                           const TaskStatus& status,
                           double resolutionRatio = 1.0);

/**
 * \brief The oblique angle from the text edges (see measureTextEdges()), or nothing.
 *
 * It takes at least two edges that agree: a single edge cannot tell a shear from a perspective.
 */
std::optional<double> findObliqueFromTextEdges(const imageproc::BinaryImage& deskewedMask,
                                               const TaskStatus& status,
                                               double resolutionRatio = 1.0);

}  // namespace deskew

#endif
