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
/** (d): the edge directions looked at (± from vertical), the histogram bins and their smoothing. */
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
  const deskew::TextEdges edges(deskew::measureTextEdges(deskew::deskewedMask(image, deskewAngleDeg)));
  Finding finding;
  finding.confidence = edges.agree ? edges.count : -edges.count;
  if ((edges.count > 0) && edges.agree) {
    finding.angle = edges.angleDeg;
  }
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

QImage straightened(const QImage& gray, const double deskewAngleDeg, const double obliqueAngleDeg) {
  return unsheared(deskewedGray(gray, deskewAngleDeg), std::tan(obliqueAngleDeg * DEG2RAD)).toQImage();
}

}  // namespace compare
