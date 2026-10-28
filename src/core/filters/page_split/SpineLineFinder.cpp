// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SpineLineFinder.h"

#include <Constants.h>
#include <GrayImage.h>

#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "DebugImages.h"

namespace page_split {
using namespace imageproc;

namespace {
// Tilt sweep, in degrees.
constexpr double kMaxTiltDegrees = 2.0;
constexpr double kTiltStepDegrees = 0.25;

// The sides of a column, in downscaled (100 dpi) px: from kSideInner to kSideOuter
// pixels away. The column must be the darkest spot within kSideOuter pixels, with a
// tolerance of kLocalMinTolerance gray levels.
constexpr int kSideInner = 3;
constexpr int kSideOuter = 8;
constexpr int kLocalMinTolerance = 4;

// How much darker than the brighter side and than the darker side the column must
// be, in gray levels. The darker side may be the shadow the page runs into.
constexpr int kMinDropBrightSide = 20;
constexpr int kMinDropDarkSide = 3;

// The page height is divided into this many bands, and the band with the fewest
// matching rows decides.
constexpr int kBands = 5;

// Rows whose mean brightness in the search window is below this fraction of the
// paper brightness are scanner background, not page.
constexpr double kPageRowFraction = 0.6;
}  // namespace

QLineF SpineLineFinder::findSpine(const GrayImage& grayDownscaled,
                                  const QTransform& outToDownscaled,
                                  const QRectF& virtualImageRect,
                                  DebugImages* const dbg,
                                  std::vector<double>* const profile,
                                  double* const score) {
  if (score) {
    *score = 0.0;
  }
  if (grayDownscaled.isNull() || virtualImageRect.isEmpty()) {
    return QLineF();
  }
  bool invertible = false;
  const QTransform downscaledToOut = outToDownscaled.inverted(&invertible);
  if (!invertible) {
    return QLineF();
  }

  const int w = grayDownscaled.width();
  const int h = grayDownscaled.height();
  if (profile) {
    profile->assign(w, 0.0);
  }
  if ((w < 4 * kSideOuter) || (h < 4 * kBands)) {
    return QLineF();
  }
  const uint8_t* const data = grayDownscaled.data();
  const int stride = grayDownscaled.stride();

  const QLineF centerLine = outToDownscaled.map(QLineF(virtualImageRect.center().x(), virtualImageRect.top(),
                                                       virtualImageRect.center().x(), virtualImageRect.bottom()));
  const double centerX = centerLine.center().x();
  const double halfWindow = CENTER_WINDOW_FRACTION * w;
  const int xLo = std::max(kSideOuter + 1, static_cast<int>(centerX - halfWindow));
  const int xHi = std::min(w - kSideOuter - 2, static_cast<int>(centerX + halfWindow));
  if (xLo >= xHi) {
    return QLineF();
  }

  // Page rows: rows that are not dark scanner background. The paper brightness
  // is that of the brighter rows of the search window.
  std::vector<double> rowMean(h, 0.0);
  for (int y = 0; y < h; ++y) {
    const uint8_t* row = data + y * stride;
    long long sum = 0;
    for (int x = xLo; x <= xHi; ++x) {
      sum += row[x];
    }
    rowMean[y] = static_cast<double>(sum) / (xHi - xLo + 1);
  }
  std::vector<double> sortedMeans(rowMean);
  std::sort(sortedMeans.begin(), sortedMeans.end());
  const double paper = sortedMeans[sortedMeans.size() * 9 / 10];
  std::vector<int> pageRows;
  for (int y = 0; y < h; ++y) {
    if (rowMean[y] >= kPageRowFraction * paper) {
      pageRows.push_back(y);
    }
  }
  if (pageRows.size() < 4 * static_cast<size_t>(kBands)) {
    return QLineF();
  }

  const double halfH = 0.5 * (h - 1);
  const auto tiltSteps = static_cast<int>(std::lround(kMaxTiltDegrees / kTiltStepDegrees));
  constexpr double sideWidth = kSideOuter - kSideInner + 1;

  // Whether a row counts at a column does not depend on the tilt, so it is tested once
  // for every page row and every column a tilted line can reach.
  enum : uint8_t { OUTSIDE, NO_MATCH, MATCH };
  const auto maxShift = static_cast<int>(std::ceil(std::tan(kMaxTiltDegrees * constants::DEG2RAD) * halfH)) + 1;
  const int tableLo = std::max(0, xLo - maxShift);
  const int tableHi = std::min(w - 1, xHi + maxShift);
  const int tableWidth = tableHi - tableLo + 1;
  std::vector<uint8_t> matches(pageRows.size() * tableWidth, OUTSIDE);
  std::vector<int> rowBand(pageRows.size());
  for (size_t i = 0; i < pageRows.size(); ++i) {
    rowBand[i] = static_cast<int>(i * kBands / pageRows.size());
    const uint8_t* row = data + pageRows[i] * stride;
    uint8_t* rowMatches = matches.data() + i * tableWidth - tableLo;
    for (int x = std::max(tableLo, kSideOuter); x <= std::min(tableHi, w - 1 - kSideOuter); ++x) {
      const int column = std::min({row[x - 1], row[x], row[x + 1]});
      int localMin = 255;
      for (int k = -kSideOuter; k <= kSideOuter; ++k) {
        localMin = std::min<int>(localMin, row[x + k]);
      }
      int leftSum = 0;
      int rightSum = 0;
      for (int k = kSideInner; k <= kSideOuter; ++k) {
        leftSum += row[x - k];
        rightSum += row[x + k];
      }
      const double left = leftSum / sideWidth;
      const double right = rightSum / sideWidth;
      rowMatches[x]
          = ((column <= localMin + kLocalMinTolerance) && (std::max(left, right) - column >= kMinDropBrightSide)
             && (std::min(left, right) - column >= kMinDropDarkSide))
                ? MATCH
                : NO_MATCH;
    }
  }

  std::vector<int> rowShift(pageRows.size());
  double bestScore = -1.0;
  double bestX = 0.0;
  double bestDx = 0.0;
  // Tilts from 0 outwards (0, -1, 1, -2, 2, ...), so that of equal scores at the same position
  // the smaller tilt wins.
  for (int step = 0; step <= 2 * tiltSteps; ++step) {
    const int t = (step % 2 == 0) ? step / 2 : -(step + 1) / 2;
    // The line passes through (xc, halfH); dx is its horizontal offset at the bottom row.
    const double dx = std::tan(t * kTiltStepDegrees * constants::DEG2RAD) * halfH;
    for (size_t i = 0; i < pageRows.size(); ++i) {
      // Rounded like std::lround of the (positive) column it is added to.
      rowShift[i] = static_cast<int>(std::floor((pageRows[i] - halfH) / halfH * dx + 0.5));
    }
    for (int xc = xLo; xc <= xHi; ++xc) {
      int bandMatches[kBands] = {};
      int bandRows[kBands] = {};
      for (size_t i = 0; i < pageRows.size(); ++i) {
        const int x = xc + rowShift[i];
        if ((x < tableLo) || (x > tableHi)) {
          continue;
        }
        const uint8_t match = matches[i * tableWidth + (x - tableLo)];
        if (match == OUTSIDE) {
          continue;
        }
        ++bandRows[rowBand[i]];
        if (match == MATCH) {
          ++bandMatches[rowBand[i]];
        }
      }

      double columnScore = 1.0;
      for (int b = 0; b < kBands; ++b) {
        columnScore = std::min(columnScore, bandRows[b] > 0 ? static_cast<double>(bandMatches[b]) / bandRows[b] : 0.0);
      }
      if (profile) {
        (*profile)[xc] = std::max((*profile)[xc], columnScore);
      }
      // Of equal scores, the line closest to the center wins.
      if ((columnScore > bestScore + 1e-9)
          || ((std::fabs(columnScore - bestScore) <= 1e-9) && (std::fabs(xc - centerX) < std::fabs(bestX - centerX)))) {
        bestScore = columnScore;
        bestX = xc;
        bestDx = dx;
      }
    }
  }

  if (score) {
    *score = std::max(0.0, bestScore);
  }
  if (bestScore < MIN_SCORE) {
    return QLineF();
  }

  const QLineF spineDs(bestX - bestDx, 0.0, bestX + bestDx, h - 1);
  if (dbg) {
    QImage visual(grayDownscaled.toQImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
    QPainter painter(&visual);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(0xff, 0xff, 0x00, 0x60), 1.0));
    painter.drawLine(QLineF(xLo, 0, xLo, h - 1));
    painter.drawLine(QLineF(xHi, 0, xHi, h - 1));
    painter.setPen(QPen(QColor(0x00, 0xff, 0x00, 0xc0), 2.0));
    painter.drawLine(spineDs);
    painter.end();
    dbg->add(visual, "spine_line");
  }

  // Extend the line over the vertical extent of the page.
  const QLineF spineVirt = downscaledToOut.map(spineDs);
  const QPointF p1 = spineVirt.p1();
  const QPointF p2 = spineVirt.p2();
  if (std::fabs(p2.y() - p1.y()) < 1e-9) {
    return spineVirt;
  }
  const auto pointAtY
      = [&](const double y) { return QPointF(p1.x() + (y - p1.y()) / (p2.y() - p1.y()) * (p2.x() - p1.x()), y); };
  return QLineF(pointAtY(virtualImageRect.top()), pointAtY(virtualImageRect.bottom()));
}  // SpineLineFinder::findSpine
}  // namespace page_split
