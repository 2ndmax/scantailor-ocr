// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_OBLIQUE_COMPARE_METHODS_H_
#define SCANTAILOR_TOOLS_OBLIQUE_COMPARE_METHODS_H_

#include <optional>

namespace imageproc {
class BinaryImage;
class SkewFinder;
}  // namespace imageproc

// Candidate oblique methods for the comparison. Each takes the cleaned-up page and the deskew angle
// found for it, like deskew::Task::findObliqueAngle(), and returns the oblique angle in the same
// sense, or nothing if the page does not show it clearly enough.
namespace compare {

/** What a method finds: an angle or nothing, and how sure it is, as the method measures that. */
struct Finding {
  std::optional<double> angle;
  std::optional<double> confidence;
};

/**
 * (a) Today's sausage mask, but the measured angles are used as they are instead of being shrunk
 * by c / (c + 1). With \p checkConfidence false, also without the confidence check, to show what
 * the mask measures at all.
 */
Finding sausageFullWeight(imageproc::SkewFinder& skewFinder,
                          const imageproc::BinaryImage& image,
                          double deskewAngleDeg,
                          bool checkConfidence);

/**
 * (b) Searches the shear that makes the vertical edges of the deskewed page line up best: the
 * left and right edges of strokes are projected along the candidate direction onto a horizontal
 * axis, and the angle with the sharpest projection wins. Like SkewFinder for the lines, but for
 * the verticals (stems, line starts, justified line ends).
 */
Finding verticalEdgeSearch(const imageproc::BinaryImage& image, double deskewAngleDeg);

/**
 * (c) Fits a straight line through the starts of the text lines of the deskewed page. Lines that
 * start elsewhere (a picture beside them, centered text) are left out as outliers; a picture
 * beside the text makes a second edge, so the edge need not hold most lines. If
 * fewer than a third of the lines share one straight edge, there is no angle.
 */
Finding lineStartFit(const imageproc::BinaryImage& image, double deskewAngleDeg);

}  // namespace compare

#endif
