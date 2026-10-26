// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Methods.h"

#include <BinaryImage.h>
#include <Constants.h>
#include <GrayImage.h>
#include <SkewFinder.h>
#include <Transform.h>
#include <imageproc/OrthogonalRotation.h>

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
/** (b) gives no angle if its best score stands out less than this from the average one. */
const double VERTICAL_MIN_CONFIDENCE = 0.0;
/** (c) needs at least this many lines on one edge, spanning at least this many line heights. */
const int MIN_EDGE_LINES = 5;
const double MIN_EDGE_HEIGHT_IN_LINES = 10.0;
/** (c2): a gap in a line wider than this many line heights separates columns. */
const double COLUMN_GAP_IN_LINES = 1.5;
/** (c2): edges that lean differently by more than this are not a shear. */
const double EDGES_AGREE_DEG = 0.2;
/** (d): the windows around the estimate, in turn, and the edge pixels needed for an angle. */
const double STROKE_WINDOW_DEG[] = {10.0, 5.0, 3.0};
const int STROKE_MIN_EDGES = 2000;
/** (d): measurements, each on the page with the shear found so far undone. */
const int STROKE_PASSES = 3;

double degrees(const double tangent) {
  return std::atan(tangent) / DEG2RAD;
}

/** A point on the page, y relative to the middle of the page. */
struct Point {
  float x;
  float y;
};

/**
 * The left and right ends of the black runs of text strokes. Runs longer than \p maxRun belong to
 * pictures or rules; their ends would outweigh the text.
 */
std::vector<Point> verticalEdges(const BinaryImage& image, const int maxRun) {
  std::vector<Point> edges;
  const int w = image.width();
  const int h = image.height();
  const float middle = 0.5f * h;
  for (int y = 0; y < h; ++y) {
    int runStart = -1;
    for (int x = 0; x <= w; ++x) {
      const bool black = (x < w) && (image.getPixel(x, y) == BLACK);
      if (black && (runStart < 0)) {
        runStart = x;
      } else if (!black && (runStart >= 0)) {
        if (x - runStart <= maxRun) {
          // The edges lie between the pixels: left of the first and right of the last.
          edges.push_back({static_cast<float>(runStart) - 0.5f, static_cast<float>(y) - middle});
          edges.push_back({static_cast<float>(x) - 0.5f, static_cast<float>(y) - middle});
        }
        runStart = -1;
      }
    }
  }
  return edges;
}

/** Bins per pixel of the projection, and the smoothing in pixels. */
const int SUBBINS = 4;
const double SMOOTHING_PX = 0.7;

/**
 * How sharply the edges line up along the direction x - tangent * y = const: the sum of squares of
 * their projection onto the x axis. The projection is fine and then smoothed, so that the score does
 * not prefer angles at which the edges happen to fall onto the pixel grid.
 */
double alignmentScore(const std::vector<Point>& edges,
                      const double tangent,
                      const int width,
                      const int height,
                      std::vector<double>& histogram,
                      std::vector<double>& smoothed) {
  const double reach = std::abs(tangent) * 0.5 * height + 8.0;
  const int bins = SUBBINS * (width + static_cast<int>(std::ceil(2 * reach)) + 2);
  histogram.assign(bins, 0.0);
  for (const Point& p : edges) {
    const double u = SUBBINS * (p.x - tangent * p.y + reach);
    const int i = static_cast<int>(std::floor(u));
    const double f = u - i;
    histogram[i] += 1.0 - f;
    histogram[i + 1] += f;
  }
  static const std::vector<double> kernel = [] {
    const double sigma = SMOOTHING_PX * SUBBINS;
    const int radius = static_cast<int>(std::ceil(3 * sigma));
    std::vector<double> k(2 * radius + 1);
    for (int i = -radius; i <= radius; ++i) {
      k[i + radius] = std::exp(-0.5 * i * i / (sigma * sigma));
    }
    return k;
  }();
  const int radius = static_cast<int>(kernel.size() / 2);
  smoothed.assign(bins, 0.0);
  for (int i = 0; i < bins; ++i) {
    if (histogram[i] == 0.0) {
      continue;
    }
    for (int k = -radius; k <= radius; ++k) {
      const int j = i + k;
      if ((j >= 0) && (j < bins)) {
        smoothed[j] += histogram[i] * kernel[k + radius];
      }
    }
  }
  double score = 0;
  for (const double v : smoothed) {
    score += v * v;
  }
  return score;
}

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

/** The first column of the band with ink, or -1. */
int lineStart(const BinaryImage& image, const Band& band) {
  const std::vector<bool> ink(inkColumns(image, band));
  const auto it = std::find(ink.begin(), ink.end(), true);
  return (it == ink.end()) ? -1 : static_cast<int>(it - ink.begin());
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

Finding sausageFullWeight(SkewFinder& skewFinder,
                          const BinaryImage& image,
                          const double deskewAngleDeg,
                          const bool checkConfidence) {
  const BinaryImage mask(deskew::deskewedMask(image, deskewAngleDeg));
  if (mask.isNull()) {
    return {};
  }
  const BinaryImage rotated(orthogonalRotation(deskew::buildSausageMask(mask), 90));
  const Skew s1 = skewFinder.findSkew(mask);
  const double resolutionRatio = skewFinder.resolutionRatio();
  skewFinder.setResolutionRatio(1.0 / resolutionRatio);
  const Skew s2 = skewFinder.findSkew(rotated);
  skewFinder.setResolutionRatio(resolutionRatio);
  Finding finding;
  finding.confidence = std::min(s1.confidence(), s2.confidence());
  if (checkConfidence && (*finding.confidence < Skew::GOOD_CONFIDENCE)) {
    return finding;
  }
  const double oblique = std::clamp(s2.angle() - s1.angle(), -MAX_OBLIQUE_DEG, MAX_OBLIQUE_DEG);
  // Like deskew::Task::findObliqueAngle(): the angle of the page is the negated measurement.
  finding.angle = -oblique;
  return finding;
}

Finding verticalEdgeSearch(const BinaryImage& image, const double deskewAngleDeg) {
  const BinaryImage mask(deskew::deskewedMask(image, deskewAngleDeg));
  if (mask.isNull()) {
    return {};
  }
  // Wider than any letter of body text, narrower than a picture.
  const std::vector<Point> edges(verticalEdges(mask, std::max(4, mask.width() / 50)));
  if (edges.empty()) {
    return {};
  }
  std::vector<double> histogram;
  std::vector<double> smoothed;
  const auto score = [&](const double deg) {
    return alignmentScore(edges, std::tan(deg * DEG2RAD), mask.width(), mask.height(), histogram, smoothed);
  };

  // Coarse search over the whole range, then a fine one around the best coarse angle.
  const int coarseSteps = 50;
  const double coarseStep = MAX_OBLIQUE_DEG / coarseSteps;
  double bestDeg = 0;
  double bestScore = -1;
  double sumScores = 0;
  for (int i = -coarseSteps; i <= coarseSteps; ++i) {
    const double deg = i * coarseStep;
    const double s = score(deg);
    sumScores += s;
    if (s > bestScore) {
      bestScore = s;
      bestDeg = deg;
    }
  }
  const double meanScore = sumScores / (2 * coarseSteps + 1);
  const int fineSteps = 10;
  const double coarseBest = bestDeg;
  for (int i = -fineSteps; i <= fineSteps; ++i) {
    const double deg = coarseBest + i * coarseStep / fineSteps;
    const double s = score(deg);
    if (s > bestScore) {
      bestScore = s;
      bestDeg = deg;
    }
  }
  Finding finding;
  // Like SkewFinder: how far the best score stands out from the average one.
  finding.confidence = bestScore / meanScore - 1.0;
  if (*finding.confidence >= VERTICAL_MIN_CONFIDENCE) {
    finding.angle = bestDeg;
  }
  return finding;
}

Finding lineStartFit(const BinaryImage& image, const double deskewAngleDeg) {
  const BinaryImage mask(deskew::deskewedMask(image, deskewAngleDeg));
  if (mask.isNull()) {
    return {};
  }
  const TextLines text(textLines(mask));
  std::vector<Point> starts;
  for (const Band& line : text.lines) {
    const int x = lineStart(mask, line);
    if (x >= 0) {
      starts.push_back({static_cast<float>(x), 0.5f * (line.top + line.bottom)});
    }
  }
  const int n = static_cast<int>(starts.size());
  if (n < MIN_EDGE_LINES) {
    return {};
  }
  const Edge edge(fitEdge(starts, text.typicalHeight, n));
  Finding finding;
  finding.confidence = static_cast<double>(edge.members.size()) / n;
  if (edge.valid) {
    finding.angle = degrees(edge.tangent);
  }
  return finding;
}

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
/** One measurement of (d): the tangent of the stroke edges and the number of edge pixels used. */
struct StrokeMeasurement {
  double tangent = 0;
  int used = 0;
};

std::optional<StrokeMeasurement> measureStrokes(const GrayImage& page) {
  const int w = page.width();
  const int h = page.height();
  if ((w < 3) || (h < 3)) {
    return std::nullopt;
  }
  // The gradients of strong edges that are nearly vertical (Sobel; a full black-white step gives 1020).
  const int minMagnitude = 250;
  const double preWindow = std::tan(STROKE_WINDOW_DEG[0] * DEG2RAD);
  std::vector<std::pair<float, float>> gradients;
  const uint8_t* data = page.data();
  const int stride = page.stride();
  for (int y = 1; y < h - 1; ++y) {
    const uint8_t* above = data + (y - 1) * stride;
    const uint8_t* line = data + y * stride;
    const uint8_t* below = data + (y + 1) * stride;
    for (int x = 1; x < w - 1; ++x) {
      const int gx = (above[x + 1] + 2 * line[x + 1] + below[x + 1]) - (above[x - 1] + 2 * line[x - 1] + below[x - 1]);
      if (std::abs(gx) < minMagnitude) {
        continue;
      }
      const int gy = (below[x - 1] + 2 * below[x] + below[x + 1]) - (above[x - 1] + 2 * above[x] + above[x + 1]);
      if (std::abs(gy) <= preWindow * std::abs(gx)) {
        gradients.emplace_back(static_cast<float>(gx), static_cast<float>(gy));
      }
    }
  }

  // An edge sheared by s has the gradient (1, -tan s). Least squares of gy = -t * gx over the edges
  // within a window around the estimate, the window narrowing step by step.
  StrokeMeasurement m;
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

Finding strokeDirection(const QImage& gray, const double deskewAngleDeg) {
  const GrayImage page(deskewedGray(gray, deskewAngleDeg));
  // A single measurement falls short of the shear by about a tenth (measured with this tool), so
  // the shear found is undone and what is left is measured again.
  double tangent = 0;
  int used = 0;
  for (int pass = 0; pass < STROKE_PASSES; ++pass) {
    const std::optional<StrokeMeasurement> m(measureStrokes(pass == 0 ? page : unsheared(page, tangent)));
    if (!m) {
      return {};
    }
    tangent += m->tangent;
    used = m->used;
  }
  Finding finding;
  finding.confidence = used;
  if ((used >= STROKE_MIN_EDGES) && (std::abs(degrees(tangent)) <= MAX_OBLIQUE_DEG)) {
    finding.angle = degrees(tangent);
  }
  return finding;
}

}  // namespace compare
