// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TextLineTracer.h"

#include <Binarize.h>
#include <BinaryImage.h>
#include <Constants.h>
#include <GaussBlur.h>
#include <Grayscale.h>
#include <LocalMinMaxGeneric.h>
#include <Morphology.h>
#include <RasterOp.h>
#include <RasterOpGeneric.h>
#include <SEDM.h>
#include <Scale.h>
#include <SeedFill.h>
#include <Sobel.h>

#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <optional>

#include "AvgUnitVector.h"
#include "DebugImages.h"
#include "DetectVertContentBounds.h"
#include "DistortionModel.h"
#include "DistortionModelBuilder.h"
#include "GridLineTraverser.h"
#include "LineBoundedByRect.h"
#include "TaskStatus.h"
#include "TextLineRefiner.h"
#include "ToLineProjector.h"
#include "TowardsLineTracer.h"

using namespace imageproc;

namespace dewarping {
namespace {
// Lengths are in pixels at the 200 DPI the tracing works at.
const double kLineEndTolerance = 3.0;      // How far a line end may be from a straight edge and still agree with it.
const double kMinHeightForEdgeSlope = 30;  // Two line ends closer than this say nothing about the slope of an edge.
const double kMaxEdgeSlopeDegrees = 4.0;  // The page is already deskewed, so a steeper edge is not an edge of the text.

// The model is extrapolated beyond the traced lines, and errors grow with the distance. So it needs at least:
const int kMinTextLines = 3;
const double kMinShareOfContentHeight = 0.25;  // Of the height of the page content, which includes page numbers.
const double kMinShareOfTextHeight = 0.5;      // Of the height of the rows of text, traced or not.

// A row counts as text if its ink spans this share of the width of the text, unlike a page number.
const double kMinTextRowWidthShare = 0.4;

struct LineEnds {
  std::vector<QPointF> left;
  std::vector<QPointF> right;
};

LineEnds lineEndsOf(const std::list<std::vector<QPointF>>& polylines) {
  LineEnds ends;
  for (const std::vector<QPointF>& polyline : polylines) {
    const bool leftToRight = polyline.front().x() < polyline.back().x();
    ends.left.push_back(leftToRight ? polyline.front() : polyline.back());
    ends.right.push_back(leftToRight ? polyline.back() : polyline.front());
  }
  return ends;
}

struct EdgeDirection {
  double slope = 0;  // dx / dy
  std::vector<QPointF> agreeingEnds;
};

/** The direction most of the line ends agree on, tried through each pair of them, with the ends that agree. */
EdgeDirection mostAgreedEdgeDirection(const std::vector<QPointF>& ends) {
  const double maxSlope = std::tan(kMaxEdgeSlopeDegrees * constants::DEG2RAD);
  const auto agrees = [](const QPointF& pt, const QPointF& anchor, const double slope) {
    return std::fabs(pt.x() - (anchor.x() + slope * (pt.y() - anchor.y()))) <= kLineEndTolerance;
  };
  double bestSlope = 0;
  QPointF bestAnchor;
  int mostAgreeing = 0;
  for (size_t i = 0; i < ends.size(); ++i) {
    for (size_t j = i + 1; j < ends.size(); ++j) {
      const double dy = ends[j].y() - ends[i].y();
      if (std::fabs(dy) < kMinHeightForEdgeSlope) {
        continue;
      }
      const double slope = (ends[j].x() - ends[i].x()) / dy;
      if (std::fabs(slope) > maxSlope) {
        continue;
      }
      const auto agreeing = static_cast<int>(
          std::count_if(ends.begin(), ends.end(), [&](const QPointF& pt) { return agrees(pt, ends[i], slope); }));
      const bool sameButStraighter = (agreeing == mostAgreeing) && (std::fabs(slope) < std::fabs(bestSlope));
      if ((agreeing > mostAgreeing) || sameButStraighter) {
        bestSlope = slope;
        bestAnchor = ends[i];
        mostAgreeing = agreeing;
      }
    }
  }

  EdgeDirection best;
  best.slope = bestSlope;
  if (mostAgreeing > 0) {
    std::copy_if(ends.begin(), ends.end(), std::back_inserter(best.agreeingEnds),
                 [&](const QPointF& pt) { return agrees(pt, bestAnchor, bestSlope); });
  }
  return best;
}

/** As mostAgreedEdgeDirection(), but a tilt that may just be measuring noise counts as none. */
EdgeDirection dominantEdgeDirection(const std::vector<QPointF>& ends) {
  EdgeDirection best = mostAgreedEdgeDirection(ends);

  // A tilt the tolerance alone can explain over the height of the lines is noise. Extrapolated over the
  // page, it would visibly stretch the part of the page below a short block of text.
  const auto [top, bottom] = std::minmax_element(
      ends.begin(), ends.end(), [](const QPointF& lhs, const QPointF& rhs) { return lhs.y() < rhs.y(); });
  const double height = bottom->y() - top->y();
  if ((height > 0) && (std::fabs(best.slope) < 2.0 * kLineEndTolerance / height)) {
    best.slope = 0;
  }
  return best;
}

/** Whether enough of the \p numEnds line ends agree on the direction of an edge to trust its slope. */
bool enoughEndsAgree(const EdgeDirection& direction, const size_t numEnds) {
  // Half of them: among ragged line ends, a quarter agrees on some direction by chance (measured with
  // dewarp_compare on drawn pages of verse: 4 flat pages of 10 got a leaning edge).
  return static_cast<int>(direction.agreeingEnds.size()) >= std::max(4, static_cast<int>(numEnds) / 2);
}

/**
 * A line with the given slope through the outermost line end, as long as the image is high.
 * \p outwards is -1 for the left edge and 1 for the right one.
 */
QLineF edgeThroughOutermostEnd(const std::vector<QPointF>& ends,
                               const double slope,
                               const double outwards,
                               const int imageHeight) {
  double outermost = -std::numeric_limits<double>::max();
  for (const QPointF& end : ends) {
    outermost = std::max(outermost, outwards * (end.x() - slope * end.y()));
  }
  // One pixel beyond the outermost line end. Line ends lie off the ink by the blur of the tracing, so this
  // is not as tight as detectVertContentBounds().
  const double xAtTop = outwards * outermost + outwards;
  return QLineF(QPointF(xAtTop, 0), QPointF(xAtTop + slope * imageHeight, imageHeight));
}

/** The first and the last row whose ink spans at least \p minWidth pixels, or -1 for both if there is none. */
std::pair<int, int> verticalRangeOfWideRows(const BinaryImage& image, const double minWidth) {
  std::pair<int, int> range(-1, -1);
  const uint32_t* line = image.data();
  const uint32_t msb = uint32_t(1) << 31;
  for (int y = 0; y < image.height(); ++y, line += image.wordsPerLine()) {
    int first = -1;
    int last = -1;
    for (int x = 0; x < image.width(); ++x) {
      if (line[x >> 5] & (msb >> (x & 31))) {
        first = (first < 0) ? x : first;
        last = x;
      }
    }
    if ((first >= 0) && (last - first >= minWidth)) {
      range.first = (range.first < 0) ? y : range.first;
      range.second = y;
    }
  }
  return range;
}

/**
 * Replaces \p bounds, which come from all the content of the page, by edges through the ends of the text lines,
 * so that page numbers and indented blocks don't tilt them. Returns false, leaving \p bounds alone, if the lines
 * are too little to build a dewarping model on.
 */
bool fitBoundsToTextLines(const std::list<std::vector<QPointF>>& polylines,
                          const BinaryImage& binarized,
                          const QRect& contentRect,
                          std::pair<QLineF, QLineF>& bounds) {
  const auto numLines = static_cast<int>(polylines.size());
  if (numLines < kMinTextLines) {
    return false;
  }

  const LineEnds ends = lineEndsOf(polylines);
  double firstLineY = std::numeric_limits<double>::max();
  double lastLineY = -std::numeric_limits<double>::max();
  for (const std::vector<QPointF>& polyline : polylines) {
    double sumY = 0;
    for (const QPointF& pt : polyline) {
      sumY += pt.y();
    }
    const double lineY = sumY / polyline.size();
    firstLineY = std::min(firstLineY, lineY);
    lastLineY = std::max(lastLineY, lineY);
  }
  const double lineSpan = lastLineY - firstLineY;
  if (lineSpan < kMinShareOfContentHeight * contentRect.height()) {
    return false;
  }

  const EdgeDirection left = dominantEdgeDirection(ends.left);
  const EdgeDirection right = dominantEdgeDirection(ends.right);
  // Ragged line ends may not agree on anything. Then the other edge, or else the deskewed vertical, tells the slope.
  const bool leftReliable = enoughEndsAgree(left, ends.left.size());
  const bool rightReliable = enoughEndsAgree(right, ends.right.size());
  const double fallbackSlope = leftReliable ? left.slope : (rightReliable ? right.slope : 0.0);

  const QLineF leftEdge
      = edgeThroughOutermostEnd(ends.left, leftReliable ? left.slope : fallbackSlope, -1.0, binarized.height());
  const QLineF rightEdge
      = edgeThroughOutermostEnd(ends.right, rightReliable ? right.slope : fallbackSlope, 1.0, binarized.height());

  const double textWidth = 0.5 * ((rightEdge.x1() - leftEdge.x1()) + (rightEdge.x2() - leftEdge.x2()));
  const std::pair<int, int> textRows = verticalRangeOfWideRows(binarized, kMinTextRowWidthShare * textWidth);
  const int textHeight = textRows.second - textRows.first;
  if ((textHeight <= 0) || (lineSpan < kMinShareOfTextHeight * textHeight)) {
    return false;
  }

  bounds.first = leftEdge;
  bounds.second = rightEdge;
  return true;
}

// The trapezoid method: straight lines of text, on a page that may be distorted as a whole.
const int kMinLinesForTrapezoid = 8;
// A shorter block can't tell how lines and edges converge across the page; the model then keeps them parallel.
const double kMinShareOfContentHeightToConverge = 0.4;
// A line whose slope is this far from the general trend was traced wrongly, and must not decide the trend.
const double kMaxLineSlopeOffTrendDegrees = 0.25;
// An edge this close to a right angle with the lines belongs to a page that is turned as a whole.
const double kMaxEdgeOffRightAngleDegrees = 0.25;

struct FittedLine {
  double slope = 0;  // dy / dx
  QPointF centroid;

  double yAt(const double x) const { return centroid.y() + slope * (x - centroid.x()); }
};

FittedLine fitStraightLine(const std::vector<QPointF>& points) {
  FittedLine line;
  for (const QPointF& pt : points) {
    line.centroid += pt;
  }
  line.centroid /= static_cast<double>(points.size());

  double sumXX = 0;
  double sumXY = 0;
  for (const QPointF& pt : points) {
    sumXX += (pt.x() - line.centroid.x()) * (pt.x() - line.centroid.x());
    sumXY += (pt.x() - line.centroid.x()) * (pt.y() - line.centroid.y());
  }
  line.slope = (sumXX > 0) ? sumXY / sumXX : 0.0;
  return line;
}

/** The slope (dx / dy) of an edge, from the line ends that agree on it, or NaN if too few of them do. */
double trapezoidEdgeSlope(const std::vector<QPointF>& ends) {
  const EdgeDirection direction = mostAgreedEdgeDirection(ends);
  if (!enoughEndsAgree(direction, ends.size())) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  std::vector<QPointF> swapped;  // x and y exchanged: an edge is a function of y.
  for (const QPointF& end : direction.agreeingEnds) {
    swapped.emplace_back(end.y(), end.x());
  }
  return fitStraightLine(swapped).slope;
}

/** The line slope as a function of the height on the page, from all lines. Points are (height, slope). */
FittedLine fitLineSlopeTrend(const std::vector<QPointF>& heightsAndSlopes) {
  const FittedLine trend = fitStraightLine(heightsAndSlopes);
  const double maxOffTrend = std::tan(kMaxLineSlopeOffTrendDegrees * constants::DEG2RAD);
  std::vector<QPointF> onTrend;
  for (const QPointF& point : heightsAndSlopes) {
    if (std::fabs(point.y() - trend.yAt(point.x())) <= maxOffTrend) {
      onTrend.push_back(point);
    }
  }
  return (static_cast<int>(onTrend.size()) >= kMinLinesForTrapezoid) ? fitStraightLine(onTrend) : trend;
}

double medianOf(std::vector<double> values) {
  std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
  return values[values.size() / 2];
}

/**
 * Fits the straight top and bottom lines of a trapezoid to the traced text lines. Returns false if they are too
 * few. \p top and \p bottom get the two ends of each line, left first; \p bounds gets the edges.
 */
bool fitTrapezoid(const std::list<std::vector<QPointF>>& polylines,
                  const QRect& contentRect,
                  const int imageHeight,
                  std::pair<QLineF, QLineF>& bounds,
                  std::vector<QPointF>& top,
                  std::vector<QPointF>& bottom) {
  const auto numLines = static_cast<int>(polylines.size());
  if (numLines < kMinLinesForTrapezoid) {
    return false;
  }

  std::vector<FittedLine> lines;
  double middleX = 0;
  for (const std::vector<QPointF>& polyline : polylines) {
    lines.push_back(fitStraightLine(polyline));
    middleX += lines.back().centroid.x();
  }
  middleX /= numLines;

  std::vector<QPointF> heightsAndSlopes;
  std::vector<double> slopes;
  double topY = std::numeric_limits<double>::max();
  double bottomY = -std::numeric_limits<double>::max();
  for (const FittedLine& line : lines) {
    const double y = line.yAt(middleX);
    heightsAndSlopes.emplace_back(y, line.slope);
    slopes.push_back(line.slope);
    topY = std::min(topY, y);
    bottomY = std::max(bottomY, y);
  }

  const double medianLineSlope = medianOf(slopes);
  double topSlope = medianLineSlope;
  double bottomSlope = medianLineSlope;
  double leftSlope = 0;
  double rightSlope = 0;
  const LineEnds ends = lineEndsOf(polylines);
  if ((bottomY - topY) >= kMinShareOfContentHeightToConverge * contentRect.height()) {
    const FittedLine trend = fitLineSlopeTrend(heightsAndSlopes);
    topSlope = trend.yAt(topY);
    bottomSlope = trend.yAt(bottomY);

    leftSlope = trapezoidEdgeSlope(ends.left);
    rightSlope = trapezoidEdgeSlope(ends.right);
    // Ragged line ends may not agree on anything. One edge alone can't tell a sheared page from a trapezoid, and
    // its slope given to the other edge makes a trapezoid worse than it was. So unless both edges are known,
    // both are vertical, as the deskew step left them. The exception is a page turned as a whole: there the
    // known edge stands at right angles to the lines, and both edges follow it. The slope of the lines alone
    // must not tell the edges, since the sagging lines of a curved page lean too.
    if (std::isnan(leftSlope) || std::isnan(rightSlope)) {
      const double knownSlope = std::isnan(leftSlope) ? rightSlope : leftSlope;
      const double maxOffRightAngle = std::tan(kMaxEdgeOffRightAngleDegrees * constants::DEG2RAD);
      const bool pageIsTurned
          = !std::isnan(knownSlope) && (std::fabs(knownSlope + medianLineSlope) <= maxOffRightAngle);
      leftSlope = pageIsTurned ? knownSlope : 0.0;
      rightSlope = leftSlope;
    }
  }

  bounds.first = edgeThroughOutermostEnd(ends.left, leftSlope, -1.0, imageHeight);
  bounds.second = edgeThroughOutermostEnd(ends.right, rightSlope, 1.0, imageHeight);

  const auto lineEnds = [&](const double y, const double slope) {
    const double leftX = bounds.first.x1() + leftSlope * y;
    const double rightX = bounds.second.x1() + rightSlope * y;
    return std::vector<QPointF>{{leftX, y + slope * (leftX - middleX)}, {rightX, y + slope * (rightX - middleX)}};
  };
  top = lineEnds(topY, topSlope);
  bottom = lineEnds(bottomY, bottomSlope);
  return true;
}
// The automatic method bends a page only if its lines are bent. A single traced line is off a straight line by up
// to 10 px here and there, also on a flat page; taken as a curve of the model, it bent the page. The middle one of
// all lines is steady: at most 1.3 px on the 35 flat scans this limit was measured on, at least 5.8 px on drawn
// curved pages (dewarp_compare).
const double kMaxBowOfFlatPage = 2.5;

/**
 * How many pixels the middle of the line lies above the straight line through its ends (negative: below), by
 * the parabola that fits the line best.
 */
double bowOf(const std::vector<QPointF>& polyline) {
  const double middleX = 0.5 * (polyline.front().x() + polyline.back().x());
  const double halfLength = 0.5 * std::fabs(polyline.back().x() - polyline.front().x());
  if ((polyline.size() < 3) || (halfLength <= 0)) {
    return 0;
  }

  // y = a + b * t + bow * t^2, with t from -1 to 1 along the line. For the least squares fit, t^2 is made
  // independent of 1 and t first; what is left of it carries the bow alone.
  const auto count = static_cast<double>(polyline.size());
  const auto tOf = [&](const QPointF& pt) { return (pt.x() - middleX) / halfLength; };
  double meanT = 0;
  double meanTT = 0;
  double meanY = 0;
  for (const QPointF& pt : polyline) {
    meanT += tOf(pt) / count;
    meanTT += tOf(pt) * tOf(pt) / count;
    meanY += pt.y() / count;
  }
  double sumOfSquaredT = 0;
  double sumOfTTByT = 0;
  for (const QPointF& pt : polyline) {
    sumOfSquaredT += (tOf(pt) - meanT) * (tOf(pt) - meanT);
    sumOfTTByT += (tOf(pt) * tOf(pt) - meanTT) * (tOf(pt) - meanT);
  }
  const double shareOfT = (sumOfSquaredT > 0) ? sumOfTTByT / sumOfSquaredT : 0.0;
  double sumOfYByRest = 0;
  double sumOfSquaredRest = 0;
  for (const QPointF& pt : polyline) {
    const double rest = (tOf(pt) * tOf(pt) - meanTT) - shareOfT * (tOf(pt) - meanT);
    sumOfYByRest += (pt.y() - meanY) * rest;
    sumOfSquaredRest += rest * rest;
  }
  return (sumOfSquaredRest > 0) ? sumOfYByRest / sumOfSquaredRest : 0.0;
}

/** Whether the traced lines tell of a flat page. No lines tell of no curve. */
bool linesAreStraight(const std::list<std::vector<QPointF>>& polylines) {
  if (polylines.empty()) {
    return true;
  }
  std::vector<double> bows;
  for (const std::vector<QPointF>& polyline : polylines) {
    bows.push_back(bowOf(polyline));
  }
  return std::fabs(medianOf(bows)) <= kMaxBowOfFlatPage;
}

// A flat page whose lines all bend at one place. Scanned from a thick book, the lines run together towards the
// spine over the last part of their length: those low on the page rise there, those at its top sink, by a few
// pixels. Over the whole line that is too little for a curved page, and the trapezoid leaves it as it is.
// One traced line can't tell such a bend from its own inexactness; what all lines share at one place can.
// The limits were measured with dewarp_compare on 20 scanned pages with such a bend, on 35 scans of flat pages
// and on drawn pages (2026-11).
const int kBendPlaces = 40;  // Along the lines, from edge to edge.
// Only lines from edge to edge count: the end of a shorter line lies amid the page, and a traced line is least
// exact at its ends.
const double kMinShareOfWidthToTellABend = 0.85;
const int kMinLinesToTellABend = 8;
// This many times its uncertainty is taken off a bend, so that nothing is left of one that may as well be chance.
const double kUncertaintiesTakenOffABend = 3.0;
// In pixels at the 200 dpi of the tracing. Less is not worth giving up the straight lines of the trapezoid for.
const double kMinBendWorthBending = 1.0;

/**
 * Takes the bends that are not known from the nearest place where one is, and smooths them over three places.
 * Bends that are all unknown become none.
 */
std::vector<double> filledAndSmoothed(const std::vector<double>& bends) {
  const auto numPlaces = static_cast<int>(bends.size());
  int firstKnown = -1;
  int lastKnown = -1;
  for (int place = 0; place < numPlaces; ++place) {
    if (!std::isnan(bends[place])) {
      firstKnown = (firstKnown < 0) ? place : firstKnown;
      lastKnown = place;
    }
  }
  if (firstKnown < 0) {
    return std::vector<double>(bends.size(), 0.0);
  }
  std::vector<double> filled(bends);
  for (int place = 0; place < numPlaces; ++place) {
    if (std::isnan(filled[place])) {
      filled[place]
          = (place < firstKnown) ? filled[firstKnown] : ((place > lastKnown) ? filled[lastKnown] : filled[place - 1]);
    }
  }
  std::vector<double> smooth(bends.size());
  for (int place = 0; place < numPlaces; ++place) {
    smooth[place] = (filled[std::max(place - 1, 0)] + filled[place] + filled[std::min(place + 1, numPlaces - 1)]) / 3.0;
  }
  return smooth;
}

/**
 * Gives the straight \p top and \p bottom line of a trapezoid (two points each, left first) the bend that the
 * text lines share, or leaves them as they are if the lines share none worth it.
 */
void bendLikeTheTextLines(const std::list<std::vector<QPointF>>& polylines,
                          std::vector<QPointF>& top,
                          std::vector<QPointF>& bottom) {
  const QLineF topLine(top.front(), top.back());
  const QLineF bottomLine(bottom.front(), bottom.back());
  if ((topLine.dx() <= 0) || (bottomLine.dx() <= 0) || (bottomLine.y1() <= topLine.y1())
      || (bottomLine.y2() <= topLine.y2())) {
    return;
  }
  const auto yOn
      = [](const QLineF& line, const double x) { return line.y1() + (x - line.x1()) * line.dy() / line.dx(); };
  // The edges of the trapezoid run through the ends of its top and bottom line.
  const auto edgeXAt = [](const QPointF& onTop, const QPointF& onBottom, const double y) {
    return onTop.x() + (y - onTop.y()) * (onBottom.x() - onTop.x()) / (onBottom.y() - onTop.y());
  };

  // For each place between the edges: how far a line is off its straight line there, and its height
  // between the top line (0) and the bottom line (1). Each line counts once per place.
  std::vector<std::vector<QPointF>> heightAndOffAt(kBendPlaces);
  int numLines = 0;
  for (const std::vector<QPointF>& polyline : polylines) {
    if (polyline.size() < 2) {
      continue;
    }
    const auto [leftmost, rightmost] = std::minmax_element(
        polyline.begin(), polyline.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    const double leftEdge = edgeXAt(topLine.p1(), bottomLine.p1(), leftmost->y());
    const double rightEdge = edgeXAt(topLine.p2(), bottomLine.p2(), leftmost->y());
    if ((rightEdge <= leftEdge)
        || (rightmost->x() - leftmost->x() < kMinShareOfWidthToTellABend * (rightEdge - leftEdge))) {
      continue;
    }
    // Off the straight line the trapezoid took this text line for, so that line and bend together are the text
    // line again.
    const FittedLine straight = fitStraightLine(polyline);
    ++numLines;

    std::vector<QPointF> sums(kBendPlaces);
    std::vector<int> counts(kBendPlaces, 0);
    for (const QPointF& pt : polyline) {
      const auto place = static_cast<int>(std::floor(kBendPlaces * (pt.x() - leftEdge) / (rightEdge - leftEdge)));
      if ((place >= 0) && (place < kBendPlaces)) {
        const double topY = yOn(topLine, pt.x());
        sums[place] += QPointF((pt.y() - topY) / (yOn(bottomLine, pt.x()) - topY), pt.y() - straight.yAt(pt.x()));
        ++counts[place];
      }
    }
    for (int place = 0; place < kBendPlaces; ++place) {
      if (counts[place] > 0) {
        heightAndOffAt[place].push_back(sums[place] / counts[place]);
      }
    }
  }

  // A place counts if enough lines reach it, and half of them. There the straight line through (height, off)
  // tells the bend of the top line, at height 0, and of the bottom line, at height 1. Each bend is only as sure
  // as the lines scatter around that straight line.
  const double unknown = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> topBends(kBendPlaces, unknown);
  std::vector<double> bottomBends(kBendPlaces, unknown);
  for (int place = 0; place < kBendPlaces; ++place) {
    const std::vector<QPointF>& points = heightAndOffAt[place];
    const auto num = static_cast<int>(points.size());
    if ((num < kMinLinesToTellABend) || (num * 2 < numLines)) {
      continue;
    }
    const FittedLine offByHeight = fitStraightLine(points);
    double squaredHeights = 0;
    double squaredRest = 0;
    for (const QPointF& point : points) {
      const double fromMean = point.x() - offByHeight.centroid.x();
      const double rest = point.y() - offByHeight.yAt(point.x());
      squaredHeights += fromMean * fromMean;
      squaredRest += rest * rest;
    }
    if (squaredHeights <= 0) {
      continue;
    }
    const double scatter = std::sqrt(squaredRest / (num - 2));
    const auto surePartOfBendAt = [&](const double height) {
      const double bend = offByHeight.yAt(height);
      const double fromMean = height - offByHeight.centroid.x();
      const double uncertainty = scatter * std::sqrt(1.0 / num + fromMean * fromMean / squaredHeights);
      const double sure = std::max(0.0, std::fabs(bend) - kUncertaintiesTakenOffABend * uncertainty);
      return (bend < 0) ? -sure : sure;
    };
    topBends[place] = surePartOfBendAt(0.0);
    bottomBends[place] = surePartOfBendAt(1.0);
  }
  topBends = filledAndSmoothed(topBends);
  bottomBends = filledAndSmoothed(bottomBends);

  double largestBend = 0;
  for (int place = 0; place < kBendPlaces; ++place) {
    largestBend = std::max({largestBend, std::fabs(topBends[place]), std::fabs(bottomBends[place])});
  }
  if (largestBend < kMinBendWorthBending) {
    return;
  }

  const auto bent = [](const QLineF& straight, const std::vector<double>& bends) {
    std::vector<QPointF> curve;
    for (int i = 0; i <= kBendPlaces; ++i) {
      // The bend of a place holds at its middle; between two middles, and at the edges, the mean of the neighbors.
      const double bendHere = 0.5 * (bends[std::max(i - 1, 0)] + bends[std::min(i, kBendPlaces - 1)]);
      curve.push_back(straight.pointAt(static_cast<double>(i) / kBendPlaces) + QPointF(0, bendHere));
    }
    return curve;
  };
  top = bent(topLine, topBends);
  bottom = bent(bottomLine, bottomBends);
}
}  // namespace

struct TextLineTracer::TracedPage {
  std::list<std::vector<QPointF>> polylines;  // In the coordinates of the downscaled page, as all members here.
  std::pair<QLineF, QLineF> vertBounds;
  BinaryImage binarized;
  QRect contentRect;
  QTransform toOrig;  // From the downscaled page to the input.
};

std::optional<DistortionModel> TextLineTracer::trace(const GrayImage& input,
                                                     const Dpi& dpi,
                                                     const QRect& contentRect,
                                                     DistortionModelBuilder& output,
                                                     const TaskStatus& /*status*/,
                                                     DebugImages* dbg) {
  TracedPage page = traceTextLines(input, dpi, contentRect, dbg);
  if (linesAreStraight(page.polylines)) {
    return trapezoidOf(page, TrapezoidLines::BentLikeTheText, dbg);
  }
  giveCurvesTo(output, page, dbg);
  return std::nullopt;
}

void TextLineTracer::traceAsCurved(const GrayImage& input,
                                   const Dpi& dpi,
                                   const QRect& contentRect,
                                   DistortionModelBuilder& output,
                                   DebugImages* dbg) {
  TracedPage page = traceTextLines(input, dpi, contentRect, dbg);
  giveCurvesTo(output, page, dbg);
}

void TextLineTracer::giveCurvesTo(DistortionModelBuilder& output, TracedPage& page, DebugImages* dbg) {
  if (!fitBoundsToTextLines(page.polylines, page.binarized, page.contentRect, page.vertBounds)) {
    return;  // Without bounds and curves no model gets built, and the page is left as it is.
  }
  if (dbg) {
    dbg->add(visualizeVerticalBounds(page.binarized.toQImage(), page.vertBounds), "vertBounds_from_lines");
  }

  // Transform back to original coordinates and output.

  page.vertBounds.first = page.toOrig.map(page.vertBounds.first);
  page.vertBounds.second = page.toOrig.map(page.vertBounds.second);
  output.setVerticalBounds(page.vertBounds.first, page.vertBounds.second);

  for (std::vector<QPointF>& polyline : page.polylines) {
    for (QPointF& pt : polyline) {
      pt = page.toOrig.map(pt);
    }
    output.addHorizontalCurve(polyline);
  }
}  // TextLineTracer::giveCurvesTo

DistortionModel TextLineTracer::traceTrapezoid(const GrayImage& input,
                                               const Dpi& dpi,
                                               const QRect& contentRect,
                                               DebugImages* dbg) {
  TracedPage page = traceTextLines(input, dpi, contentRect, dbg);
  return trapezoidOf(page, TrapezoidLines::Straight, dbg);
}

std::vector<std::vector<QPointF>> TextLineTracer::tracedLines(const GrayImage& input,
                                                              const Dpi& dpi,
                                                              const QRect& contentRect) {
  const TracedPage page = traceTextLines(input, dpi, contentRect, nullptr);
  std::vector<std::vector<QPointF>> lines;
  for (const std::vector<QPointF>& polyline : page.polylines) {
    lines.emplace_back();
    for (const QPointF& pt : polyline) {
      lines.back().push_back(page.toOrig.map(pt));
    }
  }
  return lines;
}

DistortionModel TextLineTracer::trapezoidOf(TracedPage& page, const TrapezoidLines lines, DebugImages* dbg) {
  std::vector<QPointF> top;
  std::vector<QPointF> bottom;
  if (!fitTrapezoid(page.polylines, page.contentRect, page.binarized.height(), page.vertBounds, top, bottom)) {
    return DistortionModel();
  }
  if (lines == TrapezoidLines::BentLikeTheText) {
    bendLikeTheTextLines(page.polylines, top, bottom);
  }
  if (dbg) {
    dbg->add(visualizeVerticalBounds(page.binarized.toQImage(), page.vertBounds), "vertBounds_from_lines");
  }

  for (QPointF& pt : top) {
    pt = page.toOrig.map(pt);
  }
  for (QPointF& pt : bottom) {
    pt = page.toOrig.map(pt);
  }
  DistortionModel model;
  model.setTopCurve(Curve(top));
  model.setBottomCurve(Curve(bottom));
  return model;
}

TextLineTracer::TracedPage TextLineTracer::traceTextLines(const GrayImage& input,
                                                          const Dpi& dpi,
                                                          const QRect& contentRect,
                                                          DebugImages* dbg) {
  GrayImage downscaled(downscale(input, dpi));
  if (dbg) {
    dbg->add(downscaled, "downscaled");
  }

  const int downscaledWidth = downscaled.width();
  const int downscaledHeight = downscaled.height();

  const double downscaleXFactor = double(downscaledWidth) / input.width();
  const double downscaleYFactor = double(downscaledHeight) / input.height();
  QTransform toOrig;
  toOrig.scale(1.0 / downscaleXFactor, 1.0 / downscaleYFactor);

  const QRect downscaledContentRect(toOrig.inverted().mapRect(contentRect));

  BinaryImage binarized(binarizeWolf(downscaled, QSize(31, 31)));
  if (dbg) {
    dbg->add(binarized, "binarized");
  }
  // detectVertContentBounds() is sensitive to clutter and speckles, so let's try to remove it.
  sanitizeBinaryImage(binarized, downscaledContentRect);
  if (dbg) {
    dbg->add(binarized, "sanitized");
  }

  std::pair<QLineF, QLineF> vertBounds(detectVertContentBounds(binarized, dbg));
  if (dbg) {
    dbg->add(visualizeVerticalBounds(binarized.toQImage(), vertBounds), "vertBounds");
  }

  std::list<std::vector<QPointF>> polylines;
  extractTextLines(polylines, stretchGrayRange(downscaled), vertBounds, dbg);
  if (dbg) {
    dbg->add(visualizePolylines(downscaled, polylines), "traced");
  }

  // REVIEW-NOTE (2026-11): the lines are still traced, cut and filtered with the bounds of all content; a bound
  // tilted by a page number or pulled out by a figure shortens or drops lines -> the model is extrapolated there,
  // or the page is refused. Not fixed because tracing again with the bounds from the line ends changes every
  // automatic model and needs measuring first.
  filterShortCurves(polylines, vertBounds.first, vertBounds.second);
  filterOutOfBoundsCurves(polylines, vertBounds.first, vertBounds.second);
  if (dbg) {
    dbg->add(visualizePolylines(downscaled, polylines), "filtered1");
  }

  Vec2f unitDownVector(calcAvgUnitVector(vertBounds));
  unitDownVector /= std::sqrt(unitDownVector.squaredNorm());
  if (unitDownVector[1] < 0) {
    unitDownVector = -unitDownVector;
  }
  TextLineRefiner refiner(downscaled, Dpi(200, 200), unitDownVector);
  refiner.refine(polylines, /*iterations=*/100, dbg);

  filterEdgyCurves(polylines);
  if (dbg) {
    dbg->add(visualizePolylines(downscaled, polylines), "filtered2");
  }

  return {std::move(polylines), vertBounds, binarized, downscaledContentRect, toOrig};
}  // TextLineTracer::traceTextLines

GrayImage TextLineTracer::downscale(const GrayImage& input, const Dpi& dpi) {
  // Downscale to 200 DPI.
  QSize downscaledSize(input.size());
  if (dpi.isNull()) {
    // Unknown resolution: leave the size alone rather than dividing by zero.
  } else if ((dpi.horizontal() < 180) || (dpi.horizontal() > 220) || (dpi.vertical() < 180) || (dpi.vertical() > 220)) {
    downscaledSize.setWidth(std::max<int>(1, input.width() * 200 / dpi.horizontal()));
    downscaledSize.setHeight(std::max<int>(1, input.height() * 200 / dpi.vertical()));
  }
  return scaleToGray(input, downscaledSize);
}

void TextLineTracer::sanitizeBinaryImage(BinaryImage& image, const QRect& contentRect) {
  // Kill connected components touching the borders.
  BinaryImage seed(image.size(), WHITE);
  seed.fillExcept(seed.rect().adjusted(1, 1, -1, -1), BLACK);

  BinaryImage touchingBorder(seedFill(seed.release(), image, CONN8));
  rasterOp<RopSubtract<RopDst, RopSrc>>(image, touchingBorder.release());

  // Poor man's despeckle.
  BinaryImage contentSeeds(openBrick(image, QSize(2, 3), WHITE));
  rasterOp<RopOr<RopSrc, RopDst>>(contentSeeds, openBrick(image, QSize(3, 2), WHITE));
  image = seedFill(contentSeeds.release(), image, CONN8);
  // Clear margins.
  image.fillExcept(contentRect, WHITE);
}

/**
 * Returns false if the curve contains both significant convexities and concavities.
 */
bool TextLineTracer::isCurvatureConsistent(const std::vector<QPointF>& polyline) {
  const size_t numNodes = polyline.size();

  if (numNodes <= 1) {
    // Even though we can't say anything about curvature in this case,
    // we don't like such gegenerate curves, so we reject them.
    return false;
  } else if (numNodes == 2) {
    // These are fine.
    return true;
  }
  // Threshold angle between a polyline segment and a normal to the previous one.
  const auto cosThreshold = static_cast<float>(std::cos((90.0f - 6.0f) * constants::DEG2RAD));
  const float cosSqThreshold = cosThreshold * cosThreshold;
  bool significantPositive = false;
  bool significantNegative = false;

  Vec2f prevNormal(polyline[1] - polyline[0]);
  std::swap(prevNormal[0], prevNormal[1]);
  prevNormal[0] = -prevNormal[0];
  float prevNormalSqlen = prevNormal.squaredNorm();

  for (size_t i = 1; i < numNodes - 1; ++i) {
    const Vec2f nextSegment(polyline[i + 1] - polyline[i]);
    const float nextSegmentSqlen = nextSegment.squaredNorm();

    float cosSq = 0;
    const float sqlenMult = prevNormalSqlen * nextSegmentSqlen;
    if (sqlenMult > std::numeric_limits<float>::epsilon()) {
      const float dot = prevNormal.dot(nextSegment);
      cosSq = std::fabs(dot) * dot / sqlenMult;
    }

    if (std::fabs(cosSq) >= cosSqThreshold) {
      if (cosSq > 0) {
        significantPositive = true;
      } else {
        significantNegative = true;
      }
    }

    prevNormal[0] = -nextSegment[1];
    prevNormal[1] = nextSegment[0];
    prevNormalSqlen = nextSegmentSqlen;
  }
  return !(significantPositive && significantNegative);
}  // TextLineTracer::isCurvatureConsistent

bool TextLineTracer::isInsideBounds(const QPointF& pt, const QLineF& leftBound, const QLineF& rightBound) {
  QPointF leftNormalInside(leftBound.normalVector().p2() - leftBound.p1());
  if (leftNormalInside.x() < 0) {
    leftNormalInside = -leftNormalInside;
  }
  const QPointF leftVec(pt - leftBound.p1());
  if (leftNormalInside.x() * leftVec.x() + leftNormalInside.y() * leftVec.y() < 0) {
    return false;
  }

  QPointF rightNormalInside(rightBound.normalVector().p2() - rightBound.p1());
  if (rightNormalInside.x() > 0) {
    rightNormalInside = -rightNormalInside;
  }
  const QPointF rightVec(pt - rightBound.p1());
  if (rightNormalInside.x() * rightVec.x() + rightNormalInside.y() * rightVec.y() < 0) {
    return false;
  }
  return true;
}

void TextLineTracer::filterShortCurves(std::list<std::vector<QPointF>>& polylines,
                                       const QLineF& leftBound,
                                       const QLineF& rightBound) {
  const ToLineProjector proj1(leftBound);
  const ToLineProjector proj2(rightBound);

  auto it(polylines.begin());
  const auto end(polylines.end());
  while (it != end) {
    assert(!it->empty());
    const QPointF front(it->front());
    const QPointF back(it->back());
    const double frontProjLen = proj1.projectionDist(front);
    const double backProjLen = proj2.projectionDist(back);
    const double chordLen = QLineF(front, back).length();

    // REVIEW-NOTE (2026-11): a text line that the tracing breaks at wide gaps between words is too short here
    // -> it is dropped, and "Trapezoid" or "Auto" can refuse a page depending only on where the gaps lie (a drawn
    // page: 18 lines traced, 3 left). Not fixed because this limit decides for every automatic model and would
    // have to be measured on real books.
    if (frontProjLen + backProjLen > 0.3 * chordLen) {
      polylines.erase(it++);
    } else {
      ++it;
    }
  }
}

void TextLineTracer::filterOutOfBoundsCurves(std::list<std::vector<QPointF>>& polylines,
                                             const QLineF& leftBound,
                                             const QLineF& rightBound) {
  auto it(polylines.begin());
  const auto end(polylines.end());
  while (it != end) {
    if (!isInsideBounds(it->front(), leftBound, rightBound) && !isInsideBounds(it->back(), leftBound, rightBound)) {
      polylines.erase(it++);
    } else {
      ++it;
    }
  }
}

void TextLineTracer::filterEdgyCurves(std::list<std::vector<QPointF>>& polylines) {
  auto it(polylines.begin());
  const auto end(polylines.end());
  while (it != end) {
    if (!isCurvatureConsistent(*it)) {
      polylines.erase(it++);
    } else {
      ++it;
    }
  }
}

void TextLineTracer::extractTextLines(std::list<std::vector<QPointF>>& out,
                                      const imageproc::GrayImage& image,
                                      const std::pair<QLineF, QLineF>& bounds,
                                      DebugImages* dbg) {
  const int width = image.width();
  const int height = image.height();
  const QSize size(image.size());
  const Vec2f direction(calcAvgUnitVector(bounds));
  Grid<float> mainGrid(image.width(), image.height(), 0);
  Grid<float> auxGrid(image.width(), image.height(), 0);

  const float downscale = 1.0f / (255.0f * 8.0f);
  const auto scaled = [downscale](const auto value) { return value * downscale; };
  const auto identity = [](const auto value) { return value; };
  const auto assign = [](auto& dst, const auto value) { dst = value; };
  horizontalSobel<float>(width, height, image.data(), image.stride(), scaled, auxGrid.data(), auxGrid.stride(), assign,
                         identity, mainGrid.data(), mainGrid.stride(), assign);
  verticalSobel<float>(width, height, image.data(), image.stride(), scaled, auxGrid.data(), auxGrid.stride(), assign,
                       identity, mainGrid.data(), mainGrid.stride(), [&direction](float& dst, const float value) {
                         dst = dst * direction[0] + value * direction[1];
                       });
  if (dbg) {
    dbg->add(visualizeGradient(image, mainGrid), "first_dir_deriv");
  }

  gaussBlurGeneric(size, 6.0f, 6.0f, mainGrid.data(), mainGrid.stride(), identity, mainGrid.data(), mainGrid.stride(),
                   assign);
  if (dbg) {
    dbg->add(visualizeGradient(image, mainGrid), "first_dir_deriv_blurred");
  }

  horizontalSobel<float>(width, height, mainGrid.data(), mainGrid.stride(), identity, auxGrid.data(), auxGrid.stride(),
                         assign, identity, auxGrid.data(), auxGrid.stride(), assign);
  verticalSobel<float>(width, height, mainGrid.data(), mainGrid.stride(), identity, mainGrid.data(), mainGrid.stride(),
                       assign, identity, mainGrid.data(), mainGrid.stride(), assign);
  rasterOpGeneric(auxGrid.data(), auxGrid.stride(), size, mainGrid.data(), mainGrid.stride(),
                  [&direction](const float horizontal, float& vertical) {
                    vertical = horizontal * direction[0] + vertical * direction[1];
                  });
  if (dbg) {
    dbg->add(visualizeGradient(image, mainGrid), "second_dir_deriv");
  }

  float max = 0;
  rasterOpGeneric(mainGrid.data(), mainGrid.stride(), size, [&max](const float value) {
    if (value > max) {
      max = value;
    }
  });
  const float threshold = max * 15.0f / 255.0f;

  BinaryImage initialBinarization(image.size());
  rasterOpGeneric(initialBinarization, mainGrid.data(), mainGrid.stride(),
                  [threshold](auto& bit, const float value) { bit = (value > threshold) ? uint32_t(1) : uint32_t(0); });
  if (dbg) {
    dbg->add(initialBinarization, "initialBinarization");
  }

  rasterOpGeneric(mainGrid.data(), mainGrid.stride(), size, auxGrid.data(), auxGrid.stride(),
                  [](const float value, float& abs) { abs = std::fabs(value); });
  if (dbg) {
    dbg->add(visualizeGradient(image, auxGrid), "abs");
  }

  gaussBlurGeneric(size, 12.0f, 12.0f, auxGrid.data(), auxGrid.stride(), identity, auxGrid.data(), auxGrid.stride(),
                   assign);
  if (dbg) {
    dbg->add(visualizeGradient(image, auxGrid), "blurred");
  }

  rasterOpGeneric(mainGrid.data(), mainGrid.stride(), size, auxGrid.data(), auxGrid.stride(),
                  [](const float value, float& sum) { sum += value - std::fabs(value); });
  if (dbg) {
    dbg->add(visualizeGradient(image, auxGrid), "+= diff");
  }

  BinaryImage postBinarization(image.size());
  rasterOpGeneric(postBinarization, auxGrid.data(), auxGrid.stride(),
                  [threshold](auto& bit, const float value) { bit = (value > threshold) ? uint32_t(1) : uint32_t(0); });
  if (dbg) {
    dbg->add(postBinarization, "postBinarization");
  }

  BinaryImage obstacles(image.size());
  rasterOpGeneric(obstacles, auxGrid.data(), auxGrid.stride(), [threshold](auto& bit, const float value) {
    bit = (value < -threshold) ? uint32_t(1) : uint32_t(0);
  });
  if (dbg) {
    dbg->add(obstacles, "obstacles");
  }

  Grid<float>().swap(auxGrid);  // Save memory.
  initialBinarization = closeWithObstacles(initialBinarization, obstacles, QSize(21, 21));
  if (dbg) {
    dbg->add(initialBinarization, "initial_closed");
  }

  obstacles.release();  // Save memory.
  rasterOp<RopAnd<RopDst, RopSrc>>(postBinarization, initialBinarization);
  if (dbg) {
    dbg->add(postBinarization, "post &&= initial");
  }

  initialBinarization.release();  // Save memory.
  const SEDM sedm(postBinarization);

  std::vector<QPoint> seeds;
  QLineF midLine(calcMidLine(bounds.first, bounds.second));
  findMidLineSeeds(sedm, midLine, seeds);
  if (dbg) {
    dbg->add(visualizeMidLineSeeds(image, postBinarization, bounds, midLine, seeds), "seeds");
  }

  postBinarization.release();  // Save memory.
  for (const QPoint seed : seeds) {
    std::vector<QPointF> polyline;

    {
      TowardsLineTracer tracer(&sedm, &mainGrid, bounds.first, seed);
      while (const QPoint* pt = tracer.trace(10.0f)) {
        polyline.emplace_back(*pt);
      }
      std::reverse(polyline.begin(), polyline.end());
    }

    polyline.emplace_back(seed);

    {
      TowardsLineTracer tracer(&sedm, &mainGrid, bounds.second, seed);
      while (const QPoint* pt = tracer.trace(10.0f)) {
        polyline.emplace_back(*pt);
      }
    }

    out.emplace_back();
    out.back().swap(polyline);
  }
}  // TextLineTracer::extractTextLines

BinaryImage TextLineTracer::closeWithObstacles(const BinaryImage& image,
                                               const BinaryImage& obstacles,
                                               const QSize& brick) {
  BinaryImage mask(closeBrick(image, brick));
  rasterOp<RopSubtract<RopDst, RopSrc>>(mask, obstacles);
  return seedFill(image, mask, CONN4);
}

void TextLineTracer::findMidLineSeeds(const SEDM& sedm, QLineF midLine, std::vector<QPoint>& seeds) {
  const QRect sedmRect(QPoint(0, 0), sedm.size());
  if (!lineBoundedByRect(midLine, sedmRect.adjusted(0, 0, -1, -1))) {
    // The line misses the image, or a corner was lost by rounding.
    return;
  }

  const uint32_t* sedmData = sedm.data();
  const int sedmStride = sedm.stride();

  QPoint prevPt;
  int32_t prevLevel = 0;
  int dir = 1;  // Distance growing.
  GridLineTraverser traverser(midLine);
  while (traverser.hasNext()) {
    const QPoint pt(traverser.next());
    const int32_t level = sedmData[pt.y() * sedmStride + pt.x()];
    if ((level - prevLevel) * dir < 0) {
      // Direction changed.
      if (dir > 0) {
        seeds.push_back(prevPt);
      }
      dir *= -1;
    }

    prevPt = pt;
    prevLevel = level;
  }
}

QLineF TextLineTracer::calcMidLine(const QLineF& line1, const QLineF& line2) {
  QPointF intersection;
  auto is = line1.intersects(line2, &intersection);
  if (is == QLineF::NoIntersection) {
    // Lines are parallel.
    const QPointF p1(line2.p1());
    const QPointF p2(ToLineProjector(line1).projectionPoint(p1));
    const QPointF origin(0.5 * (p1 + p2));
    const QPointF vector(line2.p2() - line2.p1());
    return QLineF(origin, origin + vector);
  } else {
    // Lines do intersect.
    Vec2d v1(line1.p2() - line1.p1());
    Vec2d v2(line2.p2() - line2.p1());
    v1 /= std::sqrt(v1.squaredNorm());
    v2 /= std::sqrt(v2.squaredNorm());
    return QLineF(intersection, intersection + 0.5 * (v1 + v2));
  }
}

QImage TextLineTracer::visualizeVerticalBounds(const QImage& background, const std::pair<QLineF, QLineF>& bounds) {
  QImage canvas(background.convertToFormat(QImage::Format_RGB32));

  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(Qt::blue);
  pen.setWidthF(2.0);
  painter.setPen(pen);
  painter.setOpacity(0.7);

  painter.drawLine(bounds.first);
  painter.drawLine(bounds.second);
  return canvas;
}

QImage TextLineTracer::visualizeGradient(const QImage& background, const Grid<float>& grad) {
  const int width = grad.width();
  const int height = grad.height();
  const int gradStride = grad.stride();
  // First let's find the maximum and minimum values.
  float minValue = std::numeric_limits<float>::max();
  float maxValue = std::numeric_limits<float>::lowest();

  const float* gradLine = grad.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = gradLine[x];
      if (value < minValue) {
        minValue = value;
      }
      if (value > maxValue) {
        maxValue = value;
      }
    }
    gradLine += gradStride;
  }

  float scale = std::max(maxValue, -minValue);
  if (scale > std::numeric_limits<float>::epsilon()) {
    scale = 255.0f / scale;
  }

  QImage overlay(width, height, QImage::Format_ARGB32_Premultiplied);
  auto* overlayLine = (uint32_t*) overlay.bits();
  const int overlayStride = overlay.bytesPerLine() / 4;

  gradLine = grad.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const float value = gradLine[x] * scale;
      const int magnitude = qBound(0, static_cast<int>(std::round(std::fabs(value))), 255);
      if (value < 0) {
        overlayLine[x] = qRgba(0, 0, magnitude, magnitude);
      } else {
        overlayLine[x] = qRgba(magnitude, 0, 0, magnitude);
      }
    }
    gradLine += gradStride;
    overlayLine += overlayStride;
  }

  QImage canvas(background.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  QPainter painter(&canvas);
  painter.drawImage(0, 0, overlay);
  return canvas;
}  // TextLineTracer::visualizeGradient

QImage TextLineTracer::visualizeMidLineSeeds(const QImage& background,
                                             const BinaryImage& overlay,
                                             std::pair<QLineF, QLineF> bounds,
                                             QLineF midLine,
                                             const std::vector<QPoint>& seeds) {
  QImage canvas(background.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);

  painter.drawImage(QPoint(0, 0), overlay.toAlphaMask(QColor(0xff, 0x00, 0x00, 120)));

  lineBoundedByRect(bounds.first, background.rect());
  lineBoundedByRect(bounds.second, background.rect());
  lineBoundedByRect(midLine, background.rect());

  QPen pen(QColor(0x00, 0x00, 0xff, 180));
  pen.setWidthF(5.0);
  painter.setPen(pen);
  painter.drawLine(bounds.first);
  painter.drawLine(bounds.second);

  pen.setColor(QColor(0x00, 0xff, 0x00, 180));
  painter.setPen(pen);
  painter.drawLine(midLine);

  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x2d, 0x00, 0x6d, 255));
  QRectF rect(0, 0, 7, 7);
  for (const QPoint pt : seeds) {
    rect.moveCenter(pt + QPointF(0.5, 0.5));
    painter.drawEllipse(rect);
  }
  return canvas;
}  // TextLineTracer::visualizeMidLineSeeds

QImage TextLineTracer::visualizePolylines(const QImage& background,
                                          const std::list<std::vector<QPointF>>& polylines,
                                          const std::pair<QLineF, QLineF>* vertBounds) {
  QImage canvas(background.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  QPen pen(Qt::blue);
  pen.setWidthF(3.0);
  painter.setPen(pen);

  for (const std::vector<QPointF>& polyline : polylines) {
    if (!polyline.empty()) {
      painter.drawPolyline(&polyline[0], static_cast<int>(polyline.size()));
    }
  }

  if (vertBounds) {
    painter.drawLine(vertBounds->first);
    painter.drawLine(vertBounds->second);
  }
  return canvas;
}
}  // namespace dewarping
