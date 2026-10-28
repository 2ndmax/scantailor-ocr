// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Despeckle.h"

#include <BinaryImage.h>
#include <ConnectivityMap.h>

#include <QImage>
#include <QRect>
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
 * \li Next to a big component outside the area of the text lines and on no line itself, such as a
 *     clump of dirt at the edge of the page, the reach is shorter.
 * \li If n is a letter on a line of text, c must also sit where marks of text sit relative to that
 *     line: on the baseline, above the letter, or the like (see sitsLikeAMark()). This removes
 *     specks right next to letters, which are as near as any dot of an i.
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

  int width() const { return right - left + 1; }

  int height() const { return bottom - top + 1; }

  bool isBig(const int bigSize) const { return (width() >= bigSize) || (height() >= bigSize); }
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

  // The line of text a letter stands on: its baseline and x-height. xHeight == 0: unknown.
  struct TextLine {
    int baseline = 0;
    int xHeight = 0;
  };

  bool isLetterLike(const Component& component) const;

  void buildLetterGrid();

  void findTextArea();

  bool isOffText(uint32_t label);

  const TextLine& lineOf(uint32_t letter);

  bool sitsLikeAMark(uint32_t label, uint32_t parent);

  bool hasPixelsBelow(uint32_t letter, const Component& component, int depth) const;

  bool hasDotBelow(uint32_t label, const TextLine& line) const;

  bool mayKeep(uint32_t label, uint32_t parent, uint64_t gapSq);

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
  // Letter-like components by the cell of a coarse grid their centre falls in.
  int m_gridCell = 1;
  int m_gridColumns = 0;
  std::vector<std::vector<uint32_t>> m_letterGrid;
  // Per letter-like component, once estimated.
  std::vector<TextLine> m_lines;
  std::vector<bool> m_lineEstimated;
  // Where the lines of text are; empty if there are too few.
  QRect m_textArea;
};

void Despeckler::run(BinaryImage& image) {
  m_cmap = ConnectivityMap(image, CONN8);
  if (m_cmap.maxLabel() == 0) {
    return;
  }
  m_status.throwIfCancelled();

  measureComponents();
  sortOutBigAndTiny();
  buildLetterGrid();
  findTextArea();
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

// A big component outside the text and on no line of text, such as a clump of dirt at the edge of
// the page, keeps specks only up to this part of the usual reach.
const double kOffTextReach = 0.4;

bool Despeckler::isOffText(const uint32_t label) {
  const Component& component = m_components[label];
  return component.isBig(m_settings.bigSize) && !m_textArea.isEmpty()
         && !m_textArea.contains((component.left + component.right) / 2, (component.top + component.bottom) / 2)
         && (lineOf(label).xHeight == 0);
}

// Whether a candidate may be kept for being near the kept component 'parent'.
bool Despeckler::mayKeep(const uint32_t label, const uint32_t parent, const uint64_t gapSq) {
  const Component& component = m_components[label];
  const Component& parentComponent = m_components[parent];
  double reachPerSqrtArea = m_settings.reachPerSqrtArea;
  if (isOffText(parent)) {
    reachPerSqrtArea *= kOffTextReach;
  }
  const double reach = std::min(reachPerSqrtArea * std::sqrt(double(component.area)), m_settings.maxReach);
  if (double(gapSq) > reach * reach) {
    return false;
  }
  // Big components count as large enough for anything.
  const bool largeEnoughParent = parentComponent.isBig(m_settings.bigSize)
                                 || (parentComponent.area >= m_settings.minParentRatio * component.area);
  return largeEnoughParent && sitsLikeAMark(label, parent);
}

// Letters as far as the line estimate goes: a component of about the height of letters, from the
// x-height of small print up to a few big ones.
bool Despeckler::isLetterLike(const Component& component) const {
  const int bigSize = m_settings.bigSize;
  return (component.state != Component::REMOVED) && (component.height() * 5 >= bigSize * 3)
         && (component.height() <= 8 * bigSize) && (component.width() <= 8 * bigSize);
}

void Despeckler::buildLetterGrid() {
  m_gridCell = std::max(1, 4 * m_settings.bigSize);
  m_gridColumns = m_cmap.size().width() / m_gridCell + 1;
  const int rows = m_cmap.size().height() / m_gridCell + 1;
  m_letterGrid.assign(size_t(m_gridColumns) * rows, {});
  for (uint32_t label = 1; label < m_components.size(); ++label) {
    const Component& component = m_components[label];
    if (isLetterLike(component)) {
      const int column = (component.left + component.right) / 2 / m_gridCell;
      const int row = (component.top + component.bottom) / 2 / m_gridCell;
      m_letterGrid[size_t(row) * m_gridColumns + column].push_back(label);
    }
  }
  m_lines.assign(m_components.size(), TextLine());
  m_lineEstimated.assign(m_components.size(), false);
}

// The area of the lines of text: the letters on lines, without the outermost percent on each side
// (a few specks may line up by chance), widened by a little.
void Despeckler::findTextArea() {
  std::vector<int> lefts;
  std::vector<int> rights;
  std::vector<int> tops;
  std::vector<int> bottoms;
  for (uint32_t label = 1; label < m_components.size(); ++label) {
    const Component& component = m_components[label];
    if (isLetterLike(component) && (lineOf(label).xHeight > 0)) {
      lefts.push_back(component.left);
      rights.push_back(component.right);
      tops.push_back(component.top);
      bottoms.push_back(component.bottom);
    }
  }
  if (lefts.size() < 20) {
    return;
  }
  const auto percentile = [](std::vector<int>& values, const double fraction) {
    const auto nth = values.begin() + static_cast<ptrdiff_t>(fraction * (values.size() - 1));
    std::nth_element(values.begin(), nth, values.end());
    return *nth;
  };
  const int margin = 3 * m_settings.bigSize;
  m_textArea = QRect(QPoint(percentile(lefts, 0.01) - margin, percentile(tops, 0.01) - margin),
                     QPoint(percentile(rights, 0.99) + margin, percentile(bottoms, 0.99) + margin));
}

// The line a letter-like component stands on, from the letter-like components within about a
// centimetre to the left and right and a few millimetres up and down: grouped by their bottoms, a
// group of at least three with nearly the same bottom is a line. Its baseline is their median
// bottom, its x-height a low percentile of their heights, as more letters have the x-height than
// ascenders or descenders. The letter's line is the largest one whose baseline runs through the
// letter or along its bottom (so a j or p finds it too).
const Despeckler::TextLine& Despeckler::lineOf(const uint32_t letter) {
  TextLine& result = m_lines[letter];
  if (m_lineEstimated[letter]) {
    return result;
  }
  m_lineEstimated[letter] = true;
  const Component& anchor = m_components[letter];
  if (!isLetterLike(anchor)) {
    return result;
  }

  const int reachX = 10 * m_settings.bigSize;
  const int reachY = 4 * m_settings.bigSize;
  const QRect window(QPoint(anchor.left - reachX, anchor.top - reachY),
                     QPoint(anchor.right + reachX, anchor.bottom + reachY));
  const int rows = int(m_letterGrid.size()) / m_gridColumns;
  std::vector<uint32_t> letters;
  for (int row = std::max(0, window.top() / m_gridCell); row <= std::min(rows - 1, window.bottom() / m_gridCell);
       ++row) {
    for (int column = std::max(0, window.left() / m_gridCell);
         column <= std::min(m_gridColumns - 1, window.right() / m_gridCell); ++column) {
      for (const uint32_t label : m_letterGrid[size_t(row) * m_gridColumns + column]) {
        const Component& other = m_components[label];
        if (window.contains((other.left + other.right) / 2, (other.top + other.bottom) / 2)) {
          letters.push_back(label);
        }
      }
    }
  }
  if (letters.size() < 3) {
    return result;
  }

  std::sort(letters.begin(), letters.end(),
            [this](uint32_t a, uint32_t b) { return m_components[a].bottom < m_components[b].bottom; });
  std::vector<int> heights;
  for (const uint32_t label : letters) {
    heights.push_back(m_components[label].height());
  }
  std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
  const int tolerance = std::max(1, heights[heights.size() / 2] / 5);

  size_t bestSize = 0;
  for (size_t first = 0; first < letters.size();) {
    size_t end = first + 1;
    while ((end < letters.size())
           && (m_components[letters[end]].bottom - m_components[letters[first]].bottom <= tolerance)) {
      ++end;
    }
    const size_t size = end - first;
    const int baseline = m_components[letters[first + size / 2]].bottom;
    if ((size >= 3) && (size > bestSize) && (anchor.top < baseline) && (baseline <= anchor.bottom + tolerance)) {
      bestSize = size;
      std::vector<int> lineHeights;
      for (size_t i = first; i < end; ++i) {
        lineHeights.push_back(m_components[letters[i]].height());
      }
      std::sort(lineHeights.begin(), lineHeights.end());
      result.baseline = baseline;
      result.xHeight = lineHeights[lineHeights.size() * 2 / 5];
    }
    first = end;
  }
  return result;
}

// Whether a small component sits where marks of the text sit next to the letter 'parent':
// \li on the baseline (period, dot leader) or across it (comma, low quote);
// \li above it, if wide (hyphen, dash) or over a dot on the baseline (colon, semicolon);
// \li higher than mid-height, also above the letter or beside its upper part (dot of an i or j,
//     umlaut, quote, superscript).
// A speck at mid-height beside the letter, below its line or far above it does not. Without a line
// (the parent is no letter, e.g. the dot before in a leader) or for a component of about the size
// of a letter, there is nothing to judge by.
bool Despeckler::sitsLikeAMark(const uint32_t label, const uint32_t parent) {
  const TextLine& line = lineOf(parent);
  const Component& component = m_components[label];
  const double base = line.baseline;
  const double xHeight = line.xHeight;
  if ((xHeight == 0) || (component.height() >= 0.5 * xHeight)) {
    return true;
  }
  if ((component.bottom > base + 0.6 * xHeight) || (component.top < base - 1.9 * xHeight)) {
    return false;
  }
  const bool onBaseline = component.bottom <= base + 0.1 * xHeight + 1;
  const bool acrossBaseline = component.top <= base + 1;
  if (component.bottom >= base - 0.15 * xHeight - 1) {
    return onBaseline || acrossBaseline;
  }
  if ((2 * component.width() >= 3 * component.height()) || hasDotBelow(label, line)) {
    return true;
  }
  if (component.bottom > base - 0.55 * xHeight) {
    return false;
  }
  // Above the letter: the letter itself, not just its bounding box, lies below (letters may touch,
  // such as f and ä). Beside it: the letter does not reach higher.
  const Component& letter = m_components[parent];
  const int centre = (component.left + component.right) / 2;
  const int margin = int(0.5 * xHeight) + component.width();
  const bool beside = (centre >= letter.left - margin) && (centre <= letter.right + margin)
                      && ((centre < letter.left) || (centre > letter.right)) && (letter.top > component.top);
  return beside || hasPixelsBelow(parent, component, int(xHeight));
}

// Whether the component 'letter' has pixels right below 'component', at most 'depth' pixels down.
// The columns are widened by the width of the component, as the dots of an ü sit about over the
// stems, not exactly.
bool Despeckler::hasPixelsBelow(const uint32_t letter, const Component& component, const int depth) const {
  const int left = std::max(0, component.left - component.width());
  const int right = std::min(m_cmap.size().width() - 1, component.right + component.width());
  const int bottom = std::min(m_cmap.size().height() - 1, component.bottom + depth);
  for (int y = component.bottom + 1; y <= bottom; ++y) {
    const uint32_t* line = m_cmap.data() + size_t(y) * m_cmap.stride();
    for (int x = left; x <= right; ++x) {
      if (line[x] == letter) {
        return true;
      }
    }
  }
  return false;
}

// Whether a dot of a similar size sits on the baseline right below the component, as the lower dot
// of a colon or the comma of a semicolon.
bool Despeckler::hasDotBelow(const uint32_t label, const TextLine& line) const {
  const Component& component = m_components[label];
  for (size_t i = m_firstEdge[label]; i < m_firstEdge[label + 1]; ++i) {
    const Component& other = m_components[m_edges[i].neighbour];
    const bool similar = (other.area * 3 >= component.area) && (other.area <= 3 * component.area);
    const bool below = (other.left <= component.right) && (other.right >= component.left)
                       && (other.top > component.bottom) && (other.top - component.bottom <= line.xHeight);
    const bool onBaseline
        = (other.bottom >= line.baseline - 0.3 * line.xHeight) && (other.bottom <= line.baseline + 0.6 * line.xHeight);
    if ((other.state != Component::REMOVED) && similar && below && onBaseline) {
      return true;
    }
  }
  return false;
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
