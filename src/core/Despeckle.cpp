// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Despeckle.h"

#include <BinaryImage.h>
#include <ConnectivityMap.h>

#include <QImage>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

#include "DebugImages.h"
#include "Dpi.h"
#include "FastQueue.h"
#include "TaskStatus.h"

/**
 * \file
 * Despeckling removes small connected components (8-connected) that are not near text:
 * \li A component at least \c bigSize wide or high is kept for sure.
 * \li A component of fewer than \c minArea pixels is removed for sure.
 * \li Any other component c is kept if it is near a kept component n: their gap, the distance
 *     between their nearest pixels, is at most reach(c) = min(k * sqrt(area(c)), maxReach), and
 *     area(n) >= minParentRatio * area(c). This spreads from the big components over neighbouring
 *     components, so chains such as dot leaders are kept as a whole.
 *
 * Neighbours are components whose Voronoi regions touch. A component that is cut off from kept ones
 * by others (e.g. by a row of specks) looks up the nearest pixel of a big and of any kept component
 * instead; from what that keeps, the spreading goes on.
 *
 * The sizes are in millimetres; only the factors k and minParentRatio are without unit. The values
 * of the levels were chosen with the benchmark of the dev tool despeckle_compare ("bench").
 */

using namespace imageproc;

namespace {
struct Settings {
  int bigSize;              // px
  uint32_t minArea;         // px
  double reachPerSqrtArea;  // k
  double maxReach;          // px
  double minParentRatio;

  static Settings get(double level, const Dpi& dpi);
};

// The meaning of the levels, in millimetres: CAUTIOUS, NORMAL and AGGRESSIVE are 1, 2 and 3; the
// level slider goes from 0.5 to 3.5. Between and beyond the rows the values are linear in the level.
struct LevelRow {
  double level;
  double bigSizeMm;
  double minAreaMm2;
  double reachPerSqrtArea;
  double maxReachMm;
  double minParentRatio;
};

const LevelRow kLevelTable[] = {
    {1, 0.593, 0.008, 10, 4.5, 0.125},
    {2, 1.016, 0.015, 8, 3.5, 0.35},
    {3, 1.439, 0.02, 7, 3.0, 0.4},
};

Settings Settings::get(const double level, const Dpi& dpi) {
  // The pair of rows to interpolate between (or extrapolate beyond).
  const LevelRow& a = (level < kLevelTable[1].level) ? kLevelTable[0] : kLevelTable[1];
  const LevelRow& b = (level < kLevelTable[1].level) ? kLevelTable[1] : kLevelTable[2];
  const double t = (level - a.level) / (b.level - a.level);
  const auto lerp = [t](const double from, const double to) { return std::max(0.0, from + t * (to - from)); };

  const double pxPerMm = std::min(dpi.horizontal(), dpi.vertical()) / 25.4;
  Settings settings{};
  settings.bigSize = std::max(1, int(std::lround(lerp(a.bigSizeMm, b.bigSizeMm) * pxPerMm)));
  settings.minArea = uint32_t(std::lround(lerp(a.minAreaMm2, b.minAreaMm2) * pxPerMm * pxPerMm));
  settings.reachPerSqrtArea = lerp(a.reachPerSqrtArea, b.reachPerSqrtArea);
  settings.maxReach = lerp(a.maxReachMm, b.maxReachMm) * pxPerMm;
  settings.minParentRatio = lerp(a.minParentRatio, b.minParentRatio);
  return settings;
}

double levelValue(const Despeckle::Level level) {
  switch (level) {
    case Despeckle::CAUTIOUS:
      return 1;
    case Despeckle::NORMAL:
      return 2;
    case Despeckle::AGGRESSIVE:
      return 3;
  }
  return 2;
}

struct Component {
  enum State : uint8_t { CANDIDATE, KEPT, REMOVED };

  uint32_t area = 0;
  int left = std::numeric_limits<int>::max();
  int top = std::numeric_limits<int>::max();
  int right = std::numeric_limits<int>::min();
  int bottom = std::numeric_limits<int>::min();
  State state = CANDIDATE;

  void add(const int x, const int y) {
    ++area;
    left = std::min(left, x);
    top = std::min(top, y);
    right = std::max(right, x);
    bottom = std::max(bottom, y);
  }

  bool isBig(const int bigSize) const { return (right - left + 1 >= bigSize) || (bottom - top + 1 >= bigSize); }
};

/**
 * For every pixel, the nearest pixel of a component in a label map (its seed): the seed's label and
 * the vector to it. Two passes over the image, each scanning every line both ways (8SSEDT after
 * Danielsson): nearly exact; in rare configurations a pixel gets a seed a fraction of a pixel
 * farther than the nearest one.
 *
 * REVIEW-NOTE (2026-10): the vectors are int16_t -> on pages over 32767 px wide or high with no
 * component for that long, they overflow and distances become wrong. Not fixed because such pages
 * are far beyond normal scans.
 */
class NearestSeedMap {
 public:
  struct Cell {
    uint32_t label = 0;  // 0: no seed found
    int16_t dx = 0;      // seed minus pixel
    int16_t dy = 0;
    uint32_t sq = std::numeric_limits<uint32_t>::max();  // dx * dx + dy * dy

    uint32_t distSq() const { return sq; }
  };

  // A padding of one pixel around the image, without seeds, saves checks for the image borders.
  explicit NearestSeedMap(const ConnectivityMap& seeds)
      : m_width(seeds.size().width()),
        m_height(seeds.size().height()),
        m_stride(m_width + 2),
        m_cells(size_t(m_stride) * (m_height + 2)) {
    const uint32_t* line = seeds.data();
    for (int y = 0; y < m_height; ++y, line += seeds.stride()) {
      Cell* cells = origin() + size_t(y) * m_stride;
      for (int x = 0; x < m_width; ++x) {
        if (line[x] != 0) {
          cells[x] = Cell{line[x], 0, 0, 0};
        }
      }
    }
    const int s = m_stride;
    for (int y = 0; y < m_height; ++y) {
      Cell* cells = origin() + size_t(y) * s;
      for (int x = 0; x < m_width; ++x) {
        Cell* cell = cells + x;
        offer(cell, -1, -1, 0);
        offer(cell, -s - 1, -1, -1);
        offer(cell, -s, 0, -1);
        offer(cell, -s + 1, 1, -1);
      }
      for (int x = m_width - 1; x >= 0; --x) {
        offer(cells + x, 1, 1, 0);
      }
    }
    for (int y = m_height - 1; y >= 0; --y) {
      Cell* cells = origin() + size_t(y) * s;
      for (int x = m_width - 1; x >= 0; --x) {
        Cell* cell = cells + x;
        offer(cell, 1, 1, 0);
        offer(cell, s + 1, 1, 1);
        offer(cell, s, 0, 1);
        offer(cell, s - 1, -1, 1);
      }
      for (int x = 0; x < m_width; ++x) {
        offer(cells + x, -1, -1, 0);
      }
    }
  }

  int width() const { return m_width; }

  int height() const { return m_height; }

  const Cell& at(const int x, const int y) const { return m_cells[size_t(y + 1) * m_stride + x + 1]; }

  QImage visualized() const {
    QImage image(m_width, m_height, QImage::Format_RGB32);
    for (int y = 0; y < m_height; ++y) {
      for (int x = 0; x < m_width; ++x) {
        const uint32_t label = at(x, y).label;
        image.setPixel(x, y, (label == 0) ? 0xffffffu : (0xff000000u | (label * 2654435761u >> 8)));
      }
    }
    return image;
  }

 private:
  Cell* origin() { return m_cells.data() + m_stride + 1; }

  // A cell takes the seed of its neighbour at 'offset' (ox, oy pixels away) if that one is nearer.
  static void offer(Cell* cell, const int offset, const int ox, const int oy) {
    const Cell& from = cell[offset];
    if (from.label == 0) {
      return;
    }
    const int dx = from.dx + ox;
    const int dy = from.dy + oy;
    const auto sq = uint32_t(dx * dx) + uint32_t(dy * dy);
    if (sq < cell->sq) {
      *cell = Cell{from.label, int16_t(dx), int16_t(dy), sq};
    }
  }

  int m_width;
  int m_height;
  int m_stride;
  std::vector<Cell> m_cells;
};

class Despeckler {
 public:
  Despeckler(const Settings& settings, const TaskStatus& status, DebugImages* dbg)
      : m_settings(settings), m_status(status), m_dbg(dbg) {}

  void run(BinaryImage& image);

 private:
  struct Edge {
    uint32_t neighbour;
    uint32_t gapSq;
  };

  void measureComponents();

  void sortOutBigAndTiny();

  void findNeighbours();

  bool mayKeep(uint32_t label, uint32_t parent, uint64_t gapSq) const;

  void keep(uint32_t label);

  void spread();

  bool keepNearestToKept(bool onlyBig);

  void removeUnkept(BinaryImage& image) const;

  const Settings& m_settings;
  const TaskStatus& m_status;
  DebugImages* m_dbg;
  ConnectivityMap m_cmap;
  std::vector<Component> m_components;
  // The neighbours of each component: those of label l are m_edges[m_firstEdge[l] .. m_firstEdge[l + 1]).
  std::vector<size_t> m_firstEdge;
  std::vector<Edge> m_edges;
  FastQueue<uint32_t> m_newlyKept;
};

void Despeckler::run(BinaryImage& image) {
  m_cmap = ConnectivityMap(image, CONN8);
  if (m_cmap.maxLabel() == 0) {
    return;
  }
  m_status.throwIfCancelled();

  measureComponents();
  sortOutBigAndTiny();
  m_status.throwIfCancelled();

  findNeighbours();
  m_status.throwIfCancelled();

  spread();
  m_status.throwIfCancelled();
  if (keepNearestToKept(true)) {
    spread();
  }
  // Each round costs a distance map of the page; a chain cut off more than three times is rare.
  for (int round = 0; (round < 3) && keepNearestToKept(false); ++round) {
    m_status.throwIfCancelled();
    spread();
  }
  m_status.throwIfCancelled();

  removeUnkept(image);
  if (m_dbg) {
    m_dbg->add(image, "despeckled");
  }
}

void Despeckler::measureComponents() {
  m_components.assign(m_cmap.maxLabel() + 1, Component());
  const uint32_t* line = m_cmap.data();
  for (int y = 0; y < m_cmap.size().height(); ++y, line += m_cmap.stride()) {
    for (int x = 0; x < m_cmap.size().width(); ++x) {
      if (line[x] != 0) {
        m_components[line[x]].add(x, y);
      }
    }
  }
}

// Big components are kept, tiny ones removed. The tiny ones also leave the map, so that they don't
// stand between other components when looking for neighbours.
void Despeckler::sortOutBigAndTiny() {
  for (uint32_t label = 1; label < m_components.size(); ++label) {
    Component& component = m_components[label];
    if (component.isBig(m_settings.bigSize)) {
      keep(label);
    } else if (component.area < m_settings.minArea) {
      component.state = Component::REMOVED;
    }
  }
  uint32_t* line = m_cmap.data();
  for (int y = 0; y < m_cmap.size().height(); ++y, line += m_cmap.stride()) {
    for (int x = 0; x < m_cmap.size().width(); ++x) {
      if ((line[x] != 0) && (m_components[line[x]].state == Component::REMOVED)) {
        line[x] = 0;
      }
    }
  }
  if (m_dbg) {
    m_dbg->add(m_cmap.visualized(), "components");
  }
}

// Neighbours are components whose Voronoi regions touch. The gap between them is the smallest
// distance between the seeds (nearest component pixels) of two touching cells of their regions.
void Despeckler::findNeighbours() {
  std::unordered_map<uint64_t, uint32_t> gaps;  // (lesser label, greater label) -> squared gap
  {
    const NearestSeedMap seeds(m_cmap);
    if (m_dbg) {
      m_dbg->add(seeds.visualized(), "voronoi");
    }
    const int width = seeds.width();
    const int height = seeds.height();
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const NearestSeedMap::Cell& cell = seeds.at(x, y);
        // The right and the lower neighbour; the other two see this cell as theirs.
        const std::array<std::pair<int, int>, 2> offsets{{{1, 0}, {0, 1}}};
        for (const auto& [dx, dy] : offsets) {
          if ((x + dx >= width) || (y + dy >= height)) {
            continue;
          }
          const NearestSeedMap::Cell& other = seeds.at(x + dx, y + dy);
          if ((cell.label == other.label) || (cell.label == 0) || (other.label == 0)) {
            continue;
          }
          const bool bothKept = (m_components[cell.label].state == Component::KEPT)
                                && (m_components[other.label].state == Component::KEPT);
          if (bothKept) {
            continue;
          }
          const int64_t seedDx = int64_t(cell.dx) - (dx + other.dx);
          const int64_t seedDy = int64_t(cell.dy) - (dy + other.dy);
          const auto gapSq
              = uint32_t(std::min<int64_t>(seedDx * seedDx + seedDy * seedDy, std::numeric_limits<uint32_t>::max()));
          const uint64_t key = (uint64_t(std::min(cell.label, other.label)) << 32) | std::max(cell.label, other.label);
          const auto [it, inserted] = gaps.emplace(key, gapSq);
          if (!inserted && (gapSq < it->second)) {
            it->second = gapSq;
          }
        }
      }
    }
  }

  // Into adjacency lists, both directions.
  m_firstEdge.assign(m_components.size() + 1, 0);
  for (const auto& [key, gapSq] : gaps) {
    ++m_firstEdge[key >> 32];
    ++m_firstEdge[key & 0xffffffffu];
  }
  size_t total = 0;
  for (size_t& first : m_firstEdge) {
    const size_t count = first;
    first = total;
    total += count;
  }
  m_edges.resize(total);
  std::vector<size_t> next(m_firstEdge.begin(), m_firstEdge.end() - 1);
  for (const auto& [key, gapSq] : gaps) {
    const auto a = uint32_t(key >> 32);
    const auto b = uint32_t(key & 0xffffffffu);
    m_edges[next[a]++] = {b, gapSq};
    m_edges[next[b]++] = {a, gapSq};
  }
}

// Whether a candidate may be kept for being near the kept component 'parent'.
bool Despeckler::mayKeep(const uint32_t label, const uint32_t parent, const uint64_t gapSq) const {
  const Component& component = m_components[label];
  const Component& parentComponent = m_components[parent];
  const double reach = std::min(m_settings.reachPerSqrtArea * std::sqrt(double(component.area)), m_settings.maxReach);
  if (double(gapSq) > reach * reach) {
    return false;
  }
  // Big components count as large enough for anything.
  return parentComponent.isBig(m_settings.bigSize)
         || (parentComponent.area >= m_settings.minParentRatio * component.area);
}

void Despeckler::keep(const uint32_t label) {
  m_components[label].state = Component::KEPT;
  m_newlyKept.push(label);
}

// Breadth first from the newly kept components over their neighbours.
void Despeckler::spread() {
  while (!m_newlyKept.empty()) {
    const uint32_t label = m_newlyKept.front();
    m_newlyKept.pop();
    for (size_t i = m_firstEdge[label]; i < m_firstEdge[label + 1]; ++i) {
      const Edge& edge = m_edges[i];
      if ((m_components[edge.neighbour].state == Component::CANDIDATE) && mayKeep(edge.neighbour, label, edge.gapSq)) {
        keep(edge.neighbour);
      }
    }
  }
}

// Candidates whose way to a kept component is blocked by others: looks up, for each, the nearest
// pixel of a kept component (of a big one with 'onlyBig') and keeps the candidate if that is near
// enough. Returns whether any was kept.
//
// Both lookups are needed: the nearest kept component may be a speck too small to keep the
// candidate, while a big one is a bit farther; and a dot of a leader may be cut off from the kept
// dots next to it, with no big component in reach.
bool Despeckler::keepNearestToKept(const bool onlyBig) {
  bool anyCandidate = false;
  ConnectivityMap kept(m_cmap);
  uint32_t* line = kept.data();
  for (int y = 0; y < kept.size().height(); ++y, line += kept.stride()) {
    for (int x = 0; x < kept.size().width(); ++x) {
      const uint32_t label = line[x];
      if (label == 0) {
        continue;
      }
      const Component& component = m_components[label];
      anyCandidate |= component.state == Component::CANDIDATE;
      if ((component.state != Component::KEPT) || (onlyBig && !component.isBig(m_settings.bigSize))) {
        line[x] = 0;
      }
    }
  }
  if (!anyCandidate) {
    return false;
  }

  struct Nearest {
    uint32_t label = 0;
    uint32_t distSq = std::numeric_limits<uint32_t>::max();
  };
  std::vector<Nearest> nearest(m_components.size());
  {
    const NearestSeedMap seeds(kept);
    const uint32_t* labels = m_cmap.data();
    for (int y = 0; y < seeds.height(); ++y, labels += m_cmap.stride()) {
      for (int x = 0; x < seeds.width(); ++x) {
        const uint32_t label = labels[x];
        const NearestSeedMap::Cell& cell = seeds.at(x, y);
        if ((label == 0) || (m_components[label].state != Component::CANDIDATE) || (cell.label == 0)) {
          continue;
        }
        if (cell.distSq() < nearest[label].distSq) {
          nearest[label] = {cell.label, cell.distSq()};
        }
      }
    }
  }

  bool anyKept = false;
  for (uint32_t label = 1; label < m_components.size(); ++label) {
    if ((m_components[label].state == Component::CANDIDATE) && (nearest[label].label != 0)
        && mayKeep(label, nearest[label].label, nearest[label].distSq)) {
      keep(label);
      anyKept = true;
    }
  }
  return anyKept;
}

void Despeckler::removeUnkept(BinaryImage& image) const {
  const uint32_t msb = uint32_t(1) << 31;
  uint32_t* imageLine = image.data();
  const uint32_t* line = m_cmap.data();
  for (int y = 0; y < image.height(); ++y, imageLine += image.wordsPerLine(), line += m_cmap.stride()) {
    for (int x = 0; x < image.width(); ++x) {
      // Tiny components already left the map, as label 0.
      const bool isBlack = imageLine[x >> 5] & (msb >> (x & 31));
      if (isBlack && ((line[x] == 0) || (m_components[line[x]].state != Component::KEPT))) {
        imageLine[x >> 5] &= ~(msb >> (x & 31));
      }
    }
  }
}

void despeckleImpl(BinaryImage& image, const double level, const Dpi& dpi, const TaskStatus& status, DebugImages* dbg) {
  if ((dpi.horizontal() <= 0) || (dpi.vertical() <= 0)) {
    // Without a resolution there are no physical sizes to judge by.
    return;
  }
  const Settings settings = Settings::get(level, dpi);
  Despeckler(settings, status, dbg).run(image);
}
}  // namespace

BinaryImage Despeckle::despeckle(const BinaryImage& src,
                                 const Dpi& dpi,
                                 const Level level,
                                 const TaskStatus& status,
                                 DebugImages* const dbg) {
  BinaryImage dst(src);
  despeckleInPlace(dst, dpi, level, status, dbg);
  return dst;
}

void Despeckle::despeckleInPlace(BinaryImage& image,
                                 const Dpi& dpi,
                                 const Level level,
                                 const TaskStatus& status,
                                 DebugImages* const dbg) {
  despeckleImpl(image, levelValue(level), dpi, status, dbg);
}

imageproc::BinaryImage Despeckle::despeckle(const imageproc::BinaryImage& src,
                                            const Dpi& dpi,
                                            const double level,
                                            const TaskStatus& status,
                                            DebugImages* dbg) {
  BinaryImage dst(src);
  despeckleInPlace(dst, dpi, level, status, dbg);
  return dst;
}

void Despeckle::despeckleInPlace(imageproc::BinaryImage& image,
                                 const Dpi& dpi,
                                 const double level,
                                 const TaskStatus& status,
                                 DebugImages* dbg) {
  despeckleImpl(image, level, dpi, status, dbg);
}
