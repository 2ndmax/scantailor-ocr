// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DEWARPING_TEXTLINETRACER_H_
#define SCANTAILOR_DEWARPING_TEXTLINETRACER_H_

#include <QLineF>
#include <QPoint>
#include <QPointF>
#include <cstdint>
#include <list>
#include <optional>
#include <utility>
#include <vector>

#include "DistortionModel.h"
#include "Grid.h"
#include "VecNT.h"

class Dpi;
class QImage;
class QSize;
class QRect;
class TaskStatus;
class DebugImages;

namespace imageproc {
class BinaryImage;
class GrayImage;

class SEDM;
}  // namespace imageproc

namespace dewarping {
class DistortionModelBuilder;

class TextLineTracer {
 public:
  /**
   * The automatic method. Finds the text lines of the page and tells from them whether the page is curved.
   *
   * A curved page: returns nothing, and gives the lines and the vertical bounds of the text to \p output, for a
   * model of curves. If the lines are too few or cover too little of the page, it gives neither, so no model
   * can be built.
   *
   * A flat page: returns the model traceTrapezoid() makes of it, invalid if there are too few lines, and
   * leaves \p output alone. Single traced lines are too inexact to be the curves of a model of a flat page.
   */
  static std::optional<DistortionModel> trace(const imageproc::GrayImage& input,
                                              const Dpi& dpi,
                                              const QRect& contentRect,
                                              DistortionModelBuilder& output,
                                              const TaskStatus& status,
                                              DebugImages* dbg = nullptr);

  /**
   * Takes the text lines as straight, and returns the trapezoid they form: two straight curves in the coordinates
   * of \p input. Returns an invalid model if there are too few lines.
   */
  static DistortionModel traceTrapezoid(const imageproc::GrayImage& input,
                                        const Dpi& dpi,
                                        const QRect& contentRect,
                                        DebugImages* dbg = nullptr);

  /**
   * The traced text lines both methods start from, in the coordinates of \p input. For tools that measure
   * how straight the lines of a page are.
   */
  static std::vector<std::vector<QPointF>> tracedLines(const imageproc::GrayImage& input,
                                                       const Dpi& dpi,
                                                       const QRect& contentRect);

 private:
  // Lets dewarping_tests call the private helpers.
  friend struct TextLineTracerTestAccess;

  struct TracedPage;

  /** Everything up to the traced lines of the page. The methods differ in what they make of those. */
  static TracedPage traceTextLines(const imageproc::GrayImage& input,
                                   const Dpi& dpi,
                                   const QRect& contentRect,
                                   DebugImages* dbg);

  /** What trace() does with a curved page, for any page: dewarping_tests draws flat pages for it. */
  static void traceAsCurved(const imageproc::GrayImage& input,
                            const Dpi& dpi,
                            const QRect& contentRect,
                            DistortionModelBuilder& output,
                            DebugImages* dbg = nullptr);

  /** Gives the lines and the vertical bounds of the text to \p output, or neither if the lines are too few. */
  static void giveCurvesTo(DistortionModelBuilder& output, TracedPage& page, DebugImages* dbg);

  /** What traceTrapezoid() returns, from the traced lines. Replaces the vertical bounds of \p page by its edges. */
  static DistortionModel trapezoidOf(TracedPage& page, DebugImages* dbg);

  static imageproc::GrayImage downscale(const imageproc::GrayImage& input, const Dpi& dpi);

  static void sanitizeBinaryImage(imageproc::BinaryImage& image, const QRect& contentRect);

  static void extractTextLines(std::list<std::vector<QPointF>>& out,
                               const imageproc::GrayImage& image,
                               const std::pair<QLineF, QLineF>& bounds,
                               DebugImages* dbg);

  static imageproc::BinaryImage closeWithObstacles(const imageproc::BinaryImage& image,
                                                   const imageproc::BinaryImage& obstacles,
                                                   const QSize& brick);

  static QLineF calcMidLine(const QLineF& line1, const QLineF& line2);

  static void findMidLineSeeds(const imageproc::SEDM& sedm, QLineF midLine, std::vector<QPoint>& seeds);

  static bool isCurvatureConsistent(const std::vector<QPointF>& polyline);

  static bool isInsideBounds(const QPointF& pt, const QLineF& leftBound, const QLineF& rightBound);

  static void filterShortCurves(std::list<std::vector<QPointF>>& polylines,
                                const QLineF& leftBound,
                                const QLineF& rightBound);

  static void filterOutOfBoundsCurves(std::list<std::vector<QPointF>>& polylines,
                                      const QLineF& leftBound,
                                      const QLineF& rightBound);

  static void filterEdgyCurves(std::list<std::vector<QPointF>>& polylines);

  static QImage visualizeVerticalBounds(const QImage& background, const std::pair<QLineF, QLineF>& bounds);

  static QImage visualizeGradient(const QImage& background, const Grid<float>& grad);

  static QImage visualizeMidLineSeeds(const QImage& background,
                                      const imageproc::BinaryImage& overlay,
                                      std::pair<QLineF, QLineF> bounds,
                                      QLineF midLine,
                                      const std::vector<QPoint>& seeds);

  static QImage visualizePolylines(const QImage& background,
                                   const std::list<std::vector<QPointF>>& polylines,
                                   const std::pair<QLineF, QLineF>* vertBounds = nullptr);
};
}  // namespace dewarping
#endif  // ifndef SCANTAILOR_DEWARPING_TEXTLINETRACER_H_
