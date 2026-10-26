// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPL v3 license that can be found in the LICENSE file.
// Auto oblique (Issue #2): measured on the straight edges of the text (see ObliqueFinder.h).

#include "ObliqueFinder.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <BWColor.h>
#include <BinaryImage.h>
#include <Constants.h>
#include <imageproc/Transform.h>

#include <QImage>
#include <QPolygonF>
#include <QRect>
#include <QTransform>

namespace deskew {

using namespace imageproc;

BinaryImage deskewedMask(const BinaryImage& mask, const double deskewAngleDeg) {
  if (std::abs(deskewAngleDeg) < 1e-6) {
    return mask;
  }

  const int w = mask.width();
  const int h = mask.height();
  // Rotation about the centre. "*=" applies each step after the previous ones.
  const QPointF center(0.5 * w, 0.5 * h);
  QTransform rotXform(QTransform().translate(-center.x(), -center.y()));
  rotXform *= QTransform().rotate(deskewAngleDeg);
  rotXform *= QTransform().translate(center.x(), center.y());
  const QRectF srcRectF(0, 0, w, h);
  QPolygonF dstCorners;
  dstCorners << rotXform.map(srcRectF.topLeft()) << rotXform.map(srcRectF.topRight())
             << rotXform.map(srcRectF.bottomRight()) << rotXform.map(srcRectF.bottomLeft());
  const QRect dstRect = dstCorners.boundingRect().toAlignedRect();
  if (!dstRect.isValid()) {
    return mask;
  }
  return BinaryImage(transform(mask.toQImage(), rotXform, dstRect, OutsidePixels::assumeWeakColor(Qt::white)));
}


namespace {
using constants::DEG2RAD;

/** Larger oblique angles are not looked for, as with the oblique angle the user can set. */
const double MAX_OBLIQUE_DEG = 5.0;
/** An edge needs this many lines on it, spanning this many line heights, and a third of all lines. */
const int MIN_EDGE_LINES = 5;
const double MIN_EDGE_HEIGHT_IN_LINES = 10.0;
/** A gap in a line wider than this many line heights separates columns; spaces between words are narrower. */
const double COLUMN_GAP_IN_LINES = 1.5;
/** Edges leaning differently by more than this are no shear (measured on synthetic pages, 2026-10). */
const double EDGES_AGREE_DEG = 0.2;

double degrees(const double tangent) {
  return std::atan(tangent) / DEG2RAD;
}

/** Where a line of a column starts or ends. */
struct EdgePoint {
  float x;
  float y;
};

/** A line of text: rows with ink between empty rows. */
struct Band {
  int top;
  int bottom;  // inclusive

  int height() const { return bottom - top + 1; }
};

/** The lines of text of a page, without pictures, and the usual height of a line. */
struct TextLines {
  std::vector<Band> lines;
  double typicalHeight = 0;
};

TextLines textLines(const BinaryImage& image) {
  // A single stray pixel does not make a line.
  const int minInk = 2;
  std::vector<Band> bands;
  int top = -1;
  for (int y = 0; y < image.height(); ++y) {
    const bool ink = image.countBlackPixels(QRect(0, y, image.width(), 1)) >= minInk;
    if (ink && (top < 0)) {
      top = y;
    } else if (!ink && (top >= 0)) {
      bands.push_back({top, y - 1});
      top = -1;
    }
  }
  if (top >= 0) {
    bands.push_back({top, image.height() - 1});
  }

  TextLines result;
  if (bands.empty()) {
    return result;
  }
  std::vector<int> heights;
  for (const Band& band : bands) {
    heights.push_back(band.height());
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
  result.typicalHeight = heights[heights.size() / 2];
  // Pictures and lines merged with them are much taller than a line of text.
  for (const Band& band : bands) {
    if (band.height() <= 2.5 * result.typicalHeight) {
      result.lines.push_back(band);
    }
  }
  return result;
}

/** For each column of the page: is there ink in the line? Two black pixels, so that one stray pixel is not. */
std::vector<bool> inkColumns(const BinaryImage& image, const Band& line) {
  std::vector<bool> ink(image.width(), false);
  for (int x = 0; x < image.width(); ++x) {
    int black = 0;
    for (int y = line.top; (y <= line.bottom) && (black < 2); ++y) {
      black += (image.getPixel(x, y) == BLACK) ? 1 : 0;
    }
    ink[x] = (black >= 2);
  }
  return ink;
}

/** A straight edge that several lines start or end at: x = x0 + tangent * (y - y0), in pixels. */
struct Edge {
  bool valid = false;
  double tangent = 0;
  /** Indices of the points on the edge, in ascending order. */
  std::vector<int> members;
  /** The inverse variance of the tangent, up to a common factor: many points far apart weigh most. */
  double weight = 0;
};

/**
 * The straight edge the most of \p points lie on. Every pair of points far enough apart proposes
 * an edge; the one with the most points within a tolerance wins and is fitted by least squares.
 */
Edge fitEdge(const std::vector<EdgePoint>& points,
             const double typicalHeight,
             const int lineCount,
             const double resolutionRatio) {
  Edge edge;
  const int n = static_cast<int>(points.size());
  if (n < 2) {
    return edge;
  }
  const auto [lowest, highest] = std::minmax_element(points.begin(), points.end(),
                                                     [](const EdgePoint& a, const EdgePoint& b) { return a.y < b.y; });
  const double span = highest->y - lowest->y;
  const double tolerance = std::max(1.0, 0.08 * typicalHeight);
  const double maxTangent = std::tan((MAX_OBLIQUE_DEG + 1.0) * DEG2RAD);
  double bestResidual = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < n; ++j) {
      const double dy = points[j].y - points[i].y;
      if (dy < 0.25 * span) {
        continue;
      }
      const double tangent = (points[j].x - points[i].x) / dy;
      if (std::abs(tangent) > maxTangent) {
        continue;
      }
      std::vector<int> members;
      double residual = 0;
      for (int k = 0; k < n; ++k) {
        const double r = std::abs(points[k].x - (points[i].x + tangent * (points[k].y - points[i].y)));
        if (r <= tolerance) {
          members.push_back(k);
          residual += r;
        }
      }
      if ((members.size() > edge.members.size())
          || ((members.size() == edge.members.size()) && (residual < bestResidual))) {
        edge.members = std::move(members);
        bestResidual = residual;
      }
    }
  }
  const int count = static_cast<int>(edge.members.size());
  if ((count < MIN_EDGE_LINES) || (3 * count < lineCount)) {
    return edge;
  }

  double sy = 0;
  double sx = 0;
  double minY = points[edge.members.front()].y;
  double maxY = minY;
  for (const int k : edge.members) {
    sy += points[k].y;
    sx += points[k].x;
    minY = std::min<double>(minY, points[k].y);
    maxY = std::max<double>(maxY, points[k].y);
  }
  if (maxY - minY < MIN_EDGE_HEIGHT_IN_LINES * typicalHeight) {
    return edge;
  }
  const double my = sy / count;
  const double mx = sx / count;
  double syy = 0;
  double sxy = 0;
  for (const int k : edge.members) {
    syy += (points[k].y - my) * (points[k].y - my);
    sxy += (points[k].y - my) * (points[k].x - mx);
  }
  if (syy <= 0) {
    return edge;
  }
  edge.tangent = sxy / syy;
  edge.weight = syy;
  // Steeper edges are no oblique text but something else, such as a slanted picture.
  edge.valid = std::abs(degrees(edge.tangent / resolutionRatio)) <= MAX_OBLIQUE_DEG;
  return edge;
}

/** All edges in \p points, the strongest first: each edge found takes its points out. */
std::vector<Edge> allEdges(std::vector<EdgePoint> points,
                           const double typicalHeight,
                           const int lineCount,
                           const double resolutionRatio) {
  std::vector<Edge> edges;
  while (true) {
    const Edge edge(fitEdge(points, typicalHeight, lineCount, resolutionRatio));
    if (!edge.valid) {
      return edges;
    }
    std::vector<EdgePoint> rest;
    for (int k = 0; k < static_cast<int>(points.size()); ++k) {
      if (!std::binary_search(edge.members.begin(), edge.members.end(), k)) {
        rest.push_back(points[k]);
      }
    }
    edges.push_back(edge);
    points = std::move(rest);
  }
}
}  // namespace

TextEdges measureTextEdges(const BinaryImage& deskewedMask, const double resolutionRatio) {
  TextEdges result;
  if (deskewedMask.isNull()) {
    return result;
  }
  const TextLines text(textLines(deskewedMask));
  const int lineCount = static_cast<int>(text.lines.size());
  if (lineCount < MIN_EDGE_LINES) {
    return result;
  }
  const int columnGap = static_cast<int>(std::ceil(COLUMN_GAP_IN_LINES * text.typicalHeight));
  std::vector<EdgePoint> starts;
  std::vector<EdgePoint> ends;
  for (const Band& line : text.lines) {
    const std::vector<bool> ink(inkColumns(deskewedMask, line));
    const float y = 0.5f * (line.top + line.bottom);
    int segmentStart = -1;
    int lastInk = -1;
    for (int x = 0; x < deskewedMask.width(); ++x) {
      if (!ink[x]) {
        continue;
      }
      if (segmentStart < 0) {
        segmentStart = x;
      } else if (x - lastInk > columnGap) {
        starts.push_back({static_cast<float>(segmentStart), y});
        ends.push_back({static_cast<float>(lastInk), y});
        segmentStart = x;
      }
      lastInk = x;
    }
    if (segmentStart >= 0) {
      starts.push_back({static_cast<float>(segmentStart), y});
      ends.push_back({static_cast<float>(lastInk), y});
    }
  }

  std::vector<Edge> edges(allEdges(starts, text.typicalHeight, lineCount, resolutionRatio));
  const std::vector<Edge> rightEdges(allEdges(ends, text.typicalHeight, lineCount, resolutionRatio));
  edges.insert(edges.end(), rightEdges.begin(), rightEdges.end());
  if (edges.empty()) {
    return result;
  }
  // A pixel tangent becomes a physical one with the resolutions: dx / xdpi over dy / ydpi.
  double sumWeights = 0;
  double sumTangents = 0;
  std::vector<double> anglesDeg;
  for (const Edge& edge : edges) {
    const double tangent = edge.tangent / resolutionRatio;
    sumWeights += edge.weight;
    sumTangents += edge.weight * tangent;
    anglesDeg.push_back(degrees(tangent));
  }
  result.count = static_cast<int>(edges.size());
  result.angleDeg = degrees(sumTangents / sumWeights);
  result.agree = std::all_of(anglesDeg.begin(), anglesDeg.end(),
                             [&](const double angle) { return std::abs(angle - result.angleDeg) <= EDGES_AGREE_DEG; });
  return result;
}

std::optional<double> findObliqueFromTextEdges(const BinaryImage& deskewedMask, const double resolutionRatio) {
  const TextEdges edges(measureTextEdges(deskewedMask, resolutionRatio));
  if ((edges.count < 2) || !edges.agree) {
    return std::nullopt;
  }
  return edges.angleDeg;
}

}  // namespace deskew
