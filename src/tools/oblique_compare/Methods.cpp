// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Methods.h"

#include <BinaryImage.h>
#include <Constants.h>
#include <GrayImage.h>
#include <Transform.h>

#include <QImage>
#include <QRect>
#include <QRectF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <vector>

#include "filters/deskew/ObliqueFinder.h"

using namespace imageproc;

namespace compare {

namespace {
using constants::DEG2RAD;
const double MAX_OBLIQUE_DEG = 5.0;
/** (c2) needs at least this many lines on one edge, spanning at least this many line heights. */
const int MIN_EDGE_LINES = 5;
const double MIN_EDGE_HEIGHT_IN_LINES = 10.0;
/** (c2): a gap in a line wider than this many line heights separates columns. */
const double COLUMN_GAP_IN_LINES = 1.5;
/** (c2): edges that lean differently by more than this are not a shear. */
const double EDGES_AGREE_DEG = 0.2;
/** (d): the edge directions looked at (Â± from vertical), the histogram bins and their smoothing. */
const double STROKE_RANGE_DEG = 25.0;
const double STROKE_BIN_DEG = 0.5;
const double STROKE_SMOOTHING_BINS = 3.0;
/** (d): the windows around the most frequent direction, in turn, and the edge pixels needed. */
const double STROKE_WINDOW_DEG[] = {5.0, 3.0};
const int STROKE_MIN_EDGES = 2000;
/** (d): a second direction this far away and this high relative to the first means italics. */
const double STROKE_SECOND_PEAK_MIN_DEG = 6.0;
const double STROKE_MAX_SECOND_PEAK = 0.45;
/** (d): measurements, each on the page with the shear found so far undone. */
const int STROKE_PASSES = 3;
/** (e): a single edge counts only if the stroke direction differs from it by no more than this. */
const double EDGE_STROKES_AGREE_DEG = 0.3;

double degrees(const double tangent) {
  return std::atan(tangent) / DEG2RAD;
}

/** A point on the page, y relative to the middle of the page. */
struct Point {
  float x;
  float y;
};

/** The text lines of a page: bands of rows with ink, separated by empty rows. */
struct Band {
  int top;
  int bottom;  // inclusive

  int height() const { return bottom - top + 1; }
};

std::vector<Band> textBands(const BinaryImage& image) {
  std::vector<Band> bands;
  const int w = image.width();
  // A single stray pixel does not make a line.
  const int minInk = 2;
  int top = -1;
  for (int y = 0; y < image.height(); ++y) {
    const bool ink = image.countBlackPixels(QRect(0, y, w, 1)) >= minInk;
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
  return bands;
}

/** The lines of text of a page, without pictures, and the usual height of a line. */
struct TextLines {
  std::vector<Band> lines;
  double typicalHeight = 0;
};

TextLines textLines(const BinaryImage& image) {
  TextLines result;
  const std::vector<Band> bands(textBands(image));
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

/** For each column of the page: is there ink in the band? Two black pixels, so that one stray pixel is not. */
std::vector<bool> inkColumns(const BinaryImage& image, const Band& band) {
  std::vector<bool> ink(image.width(), false);
  for (int x = 0; x < image.width(); ++x) {
    int black = 0;
    for (int y = band.top; (y <= band.bottom) && (black < 2); ++y) {
      black += (image.getPixel(x, y) == BLACK) ? 1 : 0;
    }
    ink[x] = (black >= 2);
  }
  return ink;
}

/** A straight vertical edge that several lines start or end at: x = x0 + tangent * (y - y0). */
struct Edge {
  bool valid = false;
  double tangent = 0;
  /** Indices of the points on the edge. */
  std::vector<int> members;
  /** The inverse variance of the tangent, up to a common factor: many points far apart weigh most. */
  double weight = 0;
};

/**
 * The straight edge the most of \p points lie on. Every pair of points far enough apart proposes
 * an edge. It counts only with at least MIN_EDGE_LINES points spanning MIN_EDGE_HEIGHT_IN_LINES
 * line heights, and at least a third of \p lineCount: a few lines are too short an edge, one pixel
 * off at the start of a line tilts it by a lot.
 */
Edge fitEdge(const std::vector<Point>& points, const double typicalHeight, const int lineCount) {
  Edge edge;
  const int n = static_cast<int>(points.size());
  if (n < 2) {
    return edge;
  }
  const auto [lowest, highest]
      = std::minmax_element(points.begin(), points.end(), [](const Point& a, const Point& b) { return a.y < b.y; });
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

  // Least squares through the points on the edge.
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
  edge.valid = std::abs(degrees(edge.tangent)) <= MAX_OBLIQUE_DEG;
  return edge;
}

/** All edges in \p points, the strongest first: each edge found takes its points out. */
std::vector<Edge> allEdges(std::vector<Point> points, const double typicalHeight, const int lineCount) {
  std::vector<Edge> edges;
  while (true) {
    const Edge edge(fitEdge(points, typicalHeight, lineCount));
    if (!edge.valid) {
      return edges;
    }
    std::vector<Point> rest;
    for (int k = 0; k < static_cast<int>(points.size()); ++k) {
      if (!std::binary_search(edge.members.begin(), edge.members.end(), k)) {
        rest.push_back(points[k]);
      }
    }
    edges.push_back(edge);
    points = std::move(rest);
  }
}

/** The gray page turned by \p deskewAngleDeg about its centre, like deskew::deskewedMask(). */
GrayImage deskewedGray(const QImage& gray, const double deskewAngleDeg) {
  const int w = gray.width();
  const int h = gray.height();
  const QPointF center(0.5 * w, 0.5 * h);
  QTransform rotXform(QTransform().translate(-center.x(), -center.y()));
  rotXform *= QTransform().rotate(deskewAngleDeg);
  rotXform *= QTransform().translate(center.x(), center.y());
  const QRect dstRect(rotXform.mapRect(QRectF(0, 0, w, h)).toAlignedRect());
  return transformToGray(gray, rotXform, dstRect, OutsidePixels::assumeColor(Qt::white));
}
}  // namespace

Finding edgeFit(const BinaryImage& image, const double deskewAngleDeg) {
  const BinaryImage mask(deskew::deskewedMask(image, deskewAngleDeg));
  if (mask.isNull()) {
    return {};
  }
  const TextLines text(textLines(mask));
  const int lineCount = static_cast<int>(text.lines.size());
  if (lineCount < MIN_EDGE_LINES) {
    return {};
  }
  // A gap this wide separates columns, or text from a picture; the spaces between words are narrower.
  const int columnGap = static_cast<int>(std::ceil(COLUMN_GAP_IN_LINES * text.typicalHeight));
  std::vector<Point> starts;
  std::vector<Point> ends;
  for (const Band& line : text.lines) {
    const std::vector<bool> ink(inkColumns(mask, line));
    const float y = 0.5f * (line.top + line.bottom);
    int segmentStart = -1;
    int lastInk = -1;
    for (int x = 0; x <= mask.width(); ++x) {
      const bool isInk = (x < mask.width()) && ink[x];
      if (isInk) {
        if (segmentStart < 0) {
          segmentStart = x;
        } else if (x - lastInk > columnGap) {
          starts.push_back({static_cast<float>(segmentStart), y});
          ends.push_back({static_cast<float>(lastInk), y});
          segmentStart = x;
        }
        lastInk = x;
      }
    }
    if (segmentStart >= 0) {
      starts.push_back({static_cast<float>(segmentStart), y});
      ends.push_back({static_cast<float>(lastInk), y});
    }
  }

  std::vector<Edge> edges(allEdges(starts, text.typicalHeight, lineCount));
  const std::vector<Edge> rightEdges(allEdges(ends, text.typicalHeight, lineCount));
  edges.insert(edges.end(), rightEdges.begin(), rightEdges.end());
  Finding finding;
  finding.confidence = static_cast<double>(edges.size());
  if (edges.empty()) {
    return finding;
  }
  double sumWeights = 0;
  double sumTangents = 0;
  for (const Edge& edge : edges) {
    sumWeights += edge.weight;
    sumTangents += edge.weight * edge.tangent;
  }
  const double mean = degrees(sumTangents / sumWeights);
  // Edges leaning differently are no shear: a perspective or a curved page. Shearing such a page
  // would set one edge right and the other one wrong.
  for (const Edge& edge : edges) {
    if (std::abs(degrees(edge.tangent) - mean) > EDGES_AGREE_DEG) {
      finding.confidence = -finding.confidence.value();
      return finding;
    }
  }
  finding.angle = mean;
  return finding;
}

namespace {
/** One measurement of (d). */
struct StrokeMeasurement {
  double tangent = 0;
  /** The number of edge pixels used. */
  int used = 0;
  /** The highest other direction, at least STROKE_SECOND_PEAK_MIN_DEG away, relative to the main one. */
  double secondPeak = 0;
};

std::optional<StrokeMeasurement> measureStrokes(const GrayImage& page, const int dpi) {
  const int w = page.width();
  // A stem counts only where its edge goes on about a third of a millimetre above and below; that
  // leaves out the brackets of serifs and the curves of round letters.
  const int reach = std::max(2, static_cast<int>(std::lround(dpi / 76.2)));
  const int h = page.height();
  if ((w < 3) || (h < 2 * reach + 3)) {
    return std::nullopt;
  }
  const uint8_t* data = page.data();
  const int stride = page.stride();
  // Sobel; a full black-white step gives 1020.
  const auto gradientX = [&](const int x, const int y) {
    const uint8_t* above = data + (y - 1) * stride;
    const uint8_t* line = data + y * stride;
    const uint8_t* below = data + (y + 1) * stride;
    return (above[x + 1] + 2 * line[x + 1] + below[x + 1]) - (above[x - 1] + 2 * line[x - 1] + below[x - 1]);
  };
  const int minMagnitude = 250;
  const double preWindow = std::tan(STROKE_RANGE_DEG * DEG2RAD);
  std::vector<std::pair<float, float>> gradients;
  for (int y = reach + 1; y < h - reach - 1; ++y) {
    const uint8_t* above = data + (y - 1) * stride;
    const uint8_t* below = data + (y + 1) * stride;
    for (int x = 1; x < w - 1; ++x) {
      const int gx = gradientX(x, y);
      if (std::abs(gx) < minMagnitude) {
        continue;
      }
      const int gy = (below[x - 1] + 2 * below[x] + below[x + 1]) - (above[x - 1] + 2 * above[x] + above[x + 1]);
      if (std::abs(gy) > preWindow * std::abs(gx)) {
        continue;
      }
      const int up = gradientX(x, y - reach);
      const int down = gradientX(x, y + reach);
      const bool straight = (std::abs(up) >= minMagnitude) && (std::abs(down) >= minMagnitude) && ((up > 0) == (gx > 0))
                            && ((down > 0) == (gx > 0));
      if (straight) {
        gradients.emplace_back(static_cast<float>(gx), static_cast<float>(gy));
      }
    }
  }

  // The most frequent direction first: a histogram of the edge directions, smoothed. On a page
  // in italics, that is the slant of the italics, not the shear.
  const int bins = static_cast<int>(std::lround(2 * STROKE_RANGE_DEG / STROKE_BIN_DEG)) + 1;
  std::vector<double> histogram(bins, 0.0);
  for (const auto& [gx, gy] : gradients) {
    const int bin = static_cast<int>(std::lround((degrees(-gy / gx) + STROKE_RANGE_DEG) / STROKE_BIN_DEG));
    histogram[std::clamp(bin, 0, bins - 1)] += static_cast<double>(gx) * gx;
  }
  const int radius = static_cast<int>(std::ceil(3 * STROKE_SMOOTHING_BINS));
  std::vector<double> smoothed(bins, 0.0);
  for (int b = 0; b < bins; ++b) {
    for (int k = -radius; k <= radius; ++k) {
      if ((b + k >= 0) && (b + k < bins)) {
        smoothed[b] += histogram[b + k] * std::exp(-0.5 * k * k / (STROKE_SMOOTHING_BINS * STROKE_SMOOTHING_BINS));
      }
    }
  }
  const int peak = static_cast<int>(std::max_element(smoothed.begin(), smoothed.end()) - smoothed.begin());
  if (smoothed[peak] <= 0) {
    return std::nullopt;
  }
  StrokeMeasurement m;
  // A second direction of similar weight well away from the first: italics among upright type.
  double second = 0;
  for (int b = 0; b < bins; ++b) {
    if (std::abs(b - peak) * STROKE_BIN_DEG >= STROKE_SECOND_PEAK_MIN_DEG) {
      second = std::max(second, smoothed[b]);
    }
  }
  m.secondPeak = second / smoothed[peak];

  // Then least squares around it. An edge sheared by s has the gradient (1, -tan s): gy = -t * gx
  // over the edges within a window around the estimate, the window narrowing step by step.
  m.tangent = std::tan((peak * STROKE_BIN_DEG - STROKE_RANGE_DEG) * DEG2RAD);
  for (const double windowDeg : STROKE_WINDOW_DEG) {
    const double window = std::tan(windowDeg * DEG2RAD);
    double sxx = 0;
    double sxy = 0;
    m.used = 0;
    for (const auto& [gx, gy] : gradients) {
      if (std::abs(gy / gx + m.tangent) <= window) {
        sxx += static_cast<double>(gx) * gx;
        sxy += static_cast<double>(gx) * gy;
        ++m.used;
      }
    }
    if (sxx <= 0) {
      return std::nullopt;
    }
    m.tangent = -sxy / sxx;
  }
  return m;
}

/** The page with x - tangent * (y - middle) in place of x: undoes a shear by \p tangent. */
GrayImage unsheared(const GrayImage& page, const double tangent) {
  const QPointF center(0.5 * page.width(), 0.5 * page.height());
  QTransform xform(QTransform().translate(-center.x(), -center.y()));
  xform *= QTransform(1, 0, -tangent, 1, 0, 0);
  xform *= QTransform().translate(center.x(), center.y());
  return transformToGray(page.toQImage(), xform, QRect(QPoint(0, 0), page.size()),
                         OutsidePixels::assumeColor(Qt::white));
}
}  // namespace

Finding strokeDirection(const QImage& gray, const double deskewAngleDeg, const int dpi) {
  const GrayImage page(deskewedGray(gray, deskewAngleDeg));
  // A single measurement falls short of the shear by about a tenth (measured with this tool), so
  // the shear found is undone and what is left is measured again.
  double tangent = 0;
  StrokeMeasurement last;
  for (int pass = 0; pass < STROKE_PASSES; ++pass) {
    const std::optional<StrokeMeasurement> m(measureStrokes(pass == 0 ? page : unsheared(page, tangent), dpi));
    if (!m) {
      return {};
    }
    tangent += m->tangent;
    last = *m;
  }
  Finding finding;
  finding.confidence = last.secondPeak;
  // Italics lean on purpose: on a page with much of them, the strokes do not show the shear.
  if ((last.used >= STROKE_MIN_EDGES) && (last.secondPeak <= STROKE_MAX_SECOND_PEAK)
      && (std::abs(degrees(tangent)) <= MAX_OBLIQUE_DEG)) {
    finding.angle = degrees(tangent);
  }
  return finding;
}

Finding edgesThenStrokes(const BinaryImage& image, const QImage& gray, const double deskewAngleDeg, const int dpi) {
  const Finding edges(edgeFit(image, deskewAngleDeg));
  if (edges.confidence.value_or(0.0) < 0) {
    // The edges disagree: no shear.
    return edges;
  }
  const Finding strokes(strokeDirection(gray, deskewAngleDeg, dpi));
  if (!edges.angle) {
    return strokes;
  }
  if (*edges.confidence >= 2) {
    return edges;
  }
  // A single edge cannot tell a shear from a keystone, which leans the edges and the strokes
  // differently. The strokes have to confirm it.
  if (strokes.angle && (std::abs(*strokes.angle - *edges.angle) <= EDGE_STROKES_AGREE_DEG)) {
    return edges;
  }
  return Finding{std::nullopt, edges.confidence};
}

}  // namespace compare
