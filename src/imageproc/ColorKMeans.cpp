// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorKMeans.h"

#include <QHash>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace imageproc {
namespace {
using Lab = std::array<double, 3>;

const QRgb kWhite = 0xffffffffu;
const int kMaxIterations = 50;

// CIE Lab of an sRGB color (D65).
Lab toLab(const QRgb rgb) {
  auto linear = [](const int c) {
    const double v = c / 255.0;
    return (v <= 0.04045) ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
  };
  const double r = linear(qRed(rgb));
  const double g = linear(qGreen(rgb));
  const double b = linear(qBlue(rgb));
  const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
  const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
  auto f = [](const double t) { return (t > 0.008856) ? std::cbrt(t) : (7.787 * t + 16.0 / 116.0); };
  return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

double distance2(const Lab& a, const Lab& b) {
  return (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
}

struct Entry {
  QRgb rgb;
  int count;
  Lab lab;
  int group;
};

int nearestGroup(const Lab& lab, const std::vector<Lab>& centers) {
  int best = 0;
  for (int g = 1; g < static_cast<int>(centers.size()); ++g) {
    if (distance2(lab, centers[g]) < distance2(lab, centers[best])) {
      best = g;
    }
  }
  return best;
}
}  // namespace

QImage reduceColorsKMeans(const QImage& image, const int colors, const bool fixBlack) {
  QImage result = image.convertToFormat(QImage::Format_RGB32);
  if (result.isNull() || (colors < 1)) {
    return result;
  }

  QHash<QRgb, int> counts;
  for (int y = 0; y < result.height(); ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(result.constScanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      if (line[x] != kWhite) {
        ++counts[line[x]];
      }
    }
  }
  if (counts.isEmpty()) {
    return result;
  }
  std::vector<Entry> entries;
  entries.reserve(counts.size());
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    entries.push_back({it.key(), it.value(), toLab(it.key()), 0});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return (a.count != b.count) ? (a.count > b.count) : (a.rgb < b.rgb);
  });

  // The start: black or the most frequent color, then each time the color farthest from the
  // groups so far, weighted a little by frequency, so that single odd colors don't become a group.
  std::vector<Lab> centers;
  centers.push_back(fixBlack ? toLab(qRgb(0, 0, 0)) : entries.front().lab);
  const int wanted = std::min(colors, static_cast<int>(entries.size()) + (fixBlack ? 1 : 0));
  while (static_cast<int>(centers.size()) < wanted) {
    double farthest = -1.0;
    Lab next{};
    for (const Entry& e : entries) {
      double nearest = 1e300;
      for (const Lab& c : centers) {
        nearest = std::min(nearest, distance2(e.lab, c));
      }
      const double score = nearest * std::sqrt(static_cast<double>(e.count));
      if (score > farthest) {
        farthest = score;
        next = e.lab;
      }
    }
    if (farthest <= 0.0) {
      break;  // Fewer different colors than groups.
    }
    centers.push_back(next);
  }

  const int groups = static_cast<int>(centers.size());
  for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
    bool changed = (iteration == 0);
    for (Entry& e : entries) {
      const int group = nearestGroup(e.lab, centers);
      changed = changed || (group != e.group);
      e.group = group;
    }
    if (!changed) {
      break;
    }
    std::vector<Lab> sums(groups, Lab{0.0, 0.0, 0.0});
    std::vector<double> weights(groups, 0.0);
    for (const Entry& e : entries) {
      for (int i = 0; i < 3; ++i) {
        sums[e.group][i] += e.lab[i] * e.count;
      }
      weights[e.group] += e.count;
    }
    for (int g = (fixBlack ? 1 : 0); g < groups; ++g) {
      if (weights[g] > 0.0) {
        for (int i = 0; i < 3; ++i) {
          centers[g][i] = sums[g][i] / weights[g];
        }
      }
    }
  }

  // The color of a group is the average of its colors in RGB; the fixed black stays black.
  std::vector<std::array<double, 3>> rgbSums(groups, std::array<double, 3>{0.0, 0.0, 0.0});
  std::vector<double> weights(groups, 0.0);
  for (const Entry& e : entries) {
    const auto count = static_cast<double>(e.count);
    rgbSums[e.group][0] += qRed(e.rgb) * count;
    rgbSums[e.group][1] += qGreen(e.rgb) * count;
    rgbSums[e.group][2] += qBlue(e.rgb) * count;
    weights[e.group] += count;
  }
  std::vector<QRgb> groupColors(groups, qRgb(0, 0, 0));
  for (int g = (fixBlack ? 1 : 0); g < groups; ++g) {
    if (weights[g] > 0.0) {
      groupColors[g] = qRgb(static_cast<int>(std::lround(rgbSums[g][0] / weights[g])),
                            static_cast<int>(std::lround(rgbSums[g][1] / weights[g])),
                            static_cast<int>(std::lround(rgbSums[g][2] / weights[g])));
    }
  }
  QHash<QRgb, QRgb> mapping;
  mapping.reserve(static_cast<qsizetype>(entries.size()));
  for (const Entry& e : entries) {
    mapping.insert(e.rgb, groupColors[e.group]);
  }
  for (int y = 0; y < result.height(); ++y) {
    auto* line = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < result.width(); ++x) {
      const auto it = mapping.constFind(line[x]);
      if (it != mapping.constEnd()) {
        line[x] = it.value();
      }
    }
  }
  return result;
}
}  // namespace imageproc
