// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorKMeans.h"

#include <QHash>
#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "BinaryImage.h"
#include "ConnectivityMap.h"

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

int nearestGroup(const Lab& lab, const std::vector<Lab>& centers) {
  int best = 0;
  for (int g = 1; g < static_cast<int>(centers.size()); ++g) {
    if (distance2(lab, centers[g]) < distance2(lab, centers[best])) {
      best = g;
    }
  }
  return best;
}

// The groups found by k-means: their centers in Lab, and their colors, the average of their
// members in RGB (the fixed black stays black).
struct Palette {
  std::vector<Lab> centers;
  std::vector<QRgb> colors;

  int groupOf(const QRgb rgb) const { return nearestGroup(toLab(rgb), centers); }
};

Palette findPalette(const QHash<QRgb, int>& counts, const int colors, const bool fixBlack) {
  struct Entry {
    QRgb rgb;
    int count;
    Lab lab;
    int group;
  };
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
  Palette palette;
  std::vector<Lab>& centers = palette.centers;
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

  std::vector<std::array<double, 3>> rgbSums(groups, std::array<double, 3>{0.0, 0.0, 0.0});
  std::vector<double> weights(groups, 0.0);
  for (const Entry& e : entries) {
    const auto count = static_cast<double>(e.count);
    rgbSums[e.group][0] += qRed(e.rgb) * count;
    rgbSums[e.group][1] += qGreen(e.rgb) * count;
    rgbSums[e.group][2] += qBlue(e.rgb) * count;
    weights[e.group] += count;
  }
  palette.colors.assign(groups, qRgb(0, 0, 0));
  for (int g = (fixBlack ? 1 : 0); g < groups; ++g) {
    if (weights[g] > 0.0) {
      palette.colors[g] = qRgb(static_cast<int>(std::lround(rgbSums[g][0] / weights[g])),
                               static_cast<int>(std::lround(rgbSums[g][1] / weights[g])),
                               static_cast<int>(std::lround(rgbSums[g][2] / weights[g])));
    }
  }
  return palette;
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
  const Palette palette = findPalette(counts, colors, fixBlack);

  QHash<QRgb, QRgb> mapping;
  mapping.reserve(counts.size());
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    mapping.insert(it.key(), palette.colors[palette.groupOf(it.key())]);
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

QImage reduceTextColorsKMeans(const QImage& color,
                              const BinaryImage& text,
                              const int colors,
                              const bool fixBlack,
                              const int maxLetterPixels) {
  return reduceTextColorsKMeans(color, text, ConnectivityMap(text, CONN8), colors, fixBlack, maxLetterPixels);
}

QImage reduceTextColorsKMeans(const QImage& color,
                              const BinaryImage& text,
                              const ConnectivityMap& segments,
                              const int colors,
                              const bool fixBlack,
                              const int maxLetterPixels) {
  const QImage source = color.convertToFormat(QImage::Format_RGB32);
  QImage result(source.size(), QImage::Format_RGB32);
  result.fill(Qt::white);
  if (source.isNull() || (text.size() != source.size()) || (segments.size() != source.size()) || (colors < 1)) {
    return result;
  }
  const int width = source.width();
  const int height = source.height();

  // The groups are found from the colors of all text pixels.
  QHash<QRgb, int> counts;
  for (int y = 0; y < height; ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      if (text.getPixel(x, y) == BLACK) {
        ++counts[line[x]];
      }
    }
  }
  if (counts.isEmpty()) {
    return result;
  }
  const Palette palette = findPalette(counts, colors, fixBlack);
  QHash<QRgb, int> groupOfColor;
  groupOfColor.reserve(counts.size());
  for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
    groupOfColor.insert(it.key(), palette.groupOf(it.key()));
  }

  // Letters are the small connected areas of the text.  Each of their segments gets the group of its
  // average color; larger areas, such as pictures and the lines of drawings, keep a group for each
  // pixel and so their shading.
  const ConnectivityMap components(text, CONN8);
  std::vector<int> componentSizes(components.maxLabel() + 1, 0);
  const uint32_t* labelLine = components.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      ++componentSizes[labelLine[x]];
    }
    labelLine += components.stride();
  }

  const uint32_t maxSegment = segments.maxLabel();
  std::vector<int> segmentSizes(maxSegment + 1, 0);
  std::vector<std::array<double, 3>> sums(maxSegment + 1, std::array<double, 3>{0.0, 0.0, 0.0});
  const uint32_t* segmentLine = segments.data();
  for (int y = 0; y < height; ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    for (int x = 0; x < width; ++x) {
      const uint32_t segment = segmentLine[x];
      if (segment != 0) {
        ++segmentSizes[segment];
        sums[segment][0] += qRed(line[x]);
        sums[segment][1] += qGreen(line[x]);
        sums[segment][2] += qBlue(line[x]);
      }
    }
    segmentLine += segments.stride();
  }
  std::vector<int> segmentGroup(maxSegment + 1, -1);
  for (uint32_t segment = 1; segment <= maxSegment; ++segment) {
    if (segmentSizes[segment] > 0) {
      const double n = segmentSizes[segment];
      const QRgb average = qRgb(static_cast<int>(std::lround(sums[segment][0] / n)),
                                static_cast<int>(std::lround(sums[segment][1] / n)),
                                static_cast<int>(std::lround(sums[segment][2] / n)));
      segmentGroup[segment] = palette.groupOf(average);
    }
  }

  labelLine = components.data();
  segmentLine = segments.data();
  for (int y = 0; y < height; ++y) {
    const auto* line = reinterpret_cast<const QRgb*>(source.constScanLine(y));
    auto* resultLine = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const uint32_t label = labelLine[x];
      if (label != 0) {
        const uint32_t segment = segmentLine[x];
        // A text pixel outside every segment is colored on its own.
        const bool letter = (componentSizes[label] <= maxLetterPixels) && (segment != 0);
        const int group = letter ? segmentGroup[segment] : groupOfColor.value(line[x]);
        resultLine[x] = palette.colors[group];
      }
    }
    labelLine += components.stride();
    segmentLine += segments.stride();
  }
  return result;
}
}  // namespace imageproc
