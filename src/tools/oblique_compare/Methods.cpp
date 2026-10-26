// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Methods.h"

#include <BinaryImage.h>
#include <Constants.h>
#include <SkewFinder.h>
#include <imageproc/OrthogonalRotation.h>

#include <QRect>
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

/** The first column of the band with at least two black pixels, or -1. */
int lineStart(const BinaryImage& image, const Band& band) {
  for (int x = 0; x < image.width(); ++x) {
    int black = 0;
    for (int y = band.top; y <= band.bottom; ++y) {
      if (image.getPixel(x, y) == BLACK && ++black >= 2) {
        return x;
      }
    }
  }
  return -1;
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
  std::vector<Band> bands(textBands(mask));
  if (bands.size() < 3) {
    return {};
  }
  std::vector<int> heights;
  for (const Band& band : bands) {
    heights.push_back(band.height());
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
  const double typicalHeight = heights[heights.size() / 2];

  // Pictures and lines merged with them are much taller than a line of text.
  std::vector<Point> starts;
  for (const Band& band : bands) {
    if (band.height() > 2.5 * typicalHeight) {
      continue;
    }
    const int x = lineStart(mask, band);
    if (x >= 0) {
      starts.push_back({static_cast<float>(x), 0.5f * (band.top + band.bottom)});
    }
  }
  const int n = static_cast<int>(starts.size());
  if (n < MIN_EDGE_LINES) {
    return {};
  }

  // Every pair of lines far enough apart proposes an edge; the one most lines agree with wins.
  const double tolerance = std::max(1.0, 0.08 * typicalHeight);
  const double span = starts.back().y - starts.front().y;
  const double maxTangent = std::tan((MAX_OBLIQUE_DEG + 1.0) * DEG2RAD);
  std::vector<int> bestInliers;
  double bestResidual = 0;
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      const double dy = starts[j].y - starts[i].y;
      if (dy < 0.25 * span) {
        continue;
      }
      const double tangent = (starts[j].x - starts[i].x) / dy;
      if (std::abs(tangent) > maxTangent) {
        continue;
      }
      std::vector<int> inliers;
      double residual = 0;
      for (int k = 0; k < n; ++k) {
        const double r = std::abs(starts[k].x - (starts[i].x + tangent * (starts[k].y - starts[i].y)));
        if (r <= tolerance) {
          inliers.push_back(k);
          residual += r;
        }
      }
      if ((inliers.size() > bestInliers.size())
          || ((inliers.size() == bestInliers.size()) && (residual < bestResidual))) {
        bestInliers = std::move(inliers);
        bestResidual = residual;
      }
    }
  }
  Finding finding;
  finding.confidence = static_cast<double>(bestInliers.size()) / n;
  // A few lines are too short an edge: one pixel off at the start of a line tilts it by a lot.
  const double edgeHeight = starts[bestInliers.back()].y - starts[bestInliers.front()].y;
  if ((static_cast<int>(bestInliers.size()) < MIN_EDGE_LINES) || (3 * static_cast<int>(bestInliers.size()) < n)
      || (edgeHeight < MIN_EDGE_HEIGHT_IN_LINES * typicalHeight)) {
    return finding;
  }

  // Least squares through the lines on the edge: x = a + tangent * y.
  double sy = 0;
  double sx = 0;
  for (const int k : bestInliers) {
    sy += starts[k].y;
    sx += starts[k].x;
  }
  const double my = sy / bestInliers.size();
  const double mx = sx / bestInliers.size();
  double syy = 0;
  double sxy = 0;
  for (const int k : bestInliers) {
    syy += (starts[k].y - my) * (starts[k].y - my);
    sxy += (starts[k].y - my) * (starts[k].x - mx);
  }
  if (syy <= 0) {
    return finding;
  }
  const double angle = degrees(sxy / syy);
  if (std::abs(angle) <= MAX_OBLIQUE_DEG) {
    finding.angle = angle;
  }
  return finding;
}

}  // namespace compare
