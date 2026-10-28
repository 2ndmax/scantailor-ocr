// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Despeckle.h"

#include <BinaryImage.h>
#include <ConnectivityMap.h>

#include <QDebug>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

#include "DebugImages.h"
#include "Dpi.h"
#include "FastQueue.h"
#include "TaskStatus.h"

/**
 * \file
 * The idea of this despeckling algorithm is as follows:
 * \li The connected components that are larger than the specified threshold
 * are marked as non-garbage.
 * \li If a connected component is close enough to a non-garbage component
 * and their sizes are comparable or the non-garbage one is larger, then
 * the other one is also marked as non-garbage.
 *
 * The last step may be repeated until no new components are marked.
 * as non-garbage.
 */

using namespace imageproc;

namespace {
/**
 * We treat vertical distances differently from the horizontal ones.
 * We want horizontal proximity to have greater weight, so we
 * multiply the vertical component distances by VERTICAL_SCALE,
 * so that the distance is not:\n
 * std::sqrt(dx^2 + dy^2)\n
 * but:\n
 * std::sqrt(dx^2 + (VERTICAL_SCALE*dy)^2)\n
 * Keep in mind that we actually operate on squared distances,
 * so we don't need to take that square root.
 */
const int VERTICAL_SCALE = 2;
const int VERTICAL_SCALE_SQ = VERTICAL_SCALE * VERTICAL_SCALE;

struct Settings {
  /**
   * When multiplied by the number of pixels in a connected component,
   * gives the minimum size (in terms of the number of pixels) of a connected
   * component we may attach it to.
   */
  double minRelativeParentWeight;

  /**
   * When multiplied by the number of pixels in a connected component,
   * gives the maximum squared distance to another connected component
   * we may attach it to.
   */
  uint32_t pixelsToSqDist;

  /**
   * Defines the minimum width or height in pixels that will guarantee
   * the object won't be removed.
   */
  int bigObjectThreshold;

  static Settings get(Despeckle::Level level, const Dpi& dpi);

  static Settings get(double level, const Dpi& dpi);
};

Settings Settings::get(const Despeckle::Level level, const Dpi& dpi) {
  Settings settings{};

  const int minDpi = std::min(dpi.horizontal(), dpi.vertical());
  const double dpiFactor = minDpi / 300.0;
  // To silence compiler's warnings.
  settings.minRelativeParentWeight = 0;
  settings.pixelsToSqDist = 0;
  settings.bigObjectThreshold = 0;

  switch (level) {
    case Despeckle::CAUTIOUS:
      settings.minRelativeParentWeight = 0.125 * dpiFactor;
      settings.pixelsToSqDist = static_cast<uint32_t>(std::pow(10.0, 2));
      settings.bigObjectThreshold = qRound(7 * dpiFactor);
      break;
    case Despeckle::NORMAL:
      settings.minRelativeParentWeight = 0.175 * dpiFactor;
      settings.pixelsToSqDist = static_cast<uint32_t>(std::pow(6.5, 2));
      settings.bigObjectThreshold = qRound(12 * dpiFactor);
      break;
    case Despeckle::AGGRESSIVE:
      settings.minRelativeParentWeight = 0.225 * dpiFactor;
      settings.pixelsToSqDist = static_cast<uint32_t>(std::pow(3.5, 2));
      settings.bigObjectThreshold = qRound(17 * dpiFactor);
      break;
  }
  return settings;
}

Settings Settings::get(const double level, const Dpi& dpi) {
  Settings settings{};

  const int minDpi = std::min(dpi.horizontal(), dpi.vertical());
  const double dpiFactor = minDpi / 300.0;

  settings.minRelativeParentWeight = (0.05 * level + 0.075) * dpiFactor;
  settings.pixelsToSqDist = static_cast<uint32_t>(std::pow(0.25 * std::pow(level, 2) - 4.25 * level + 14, 2));
  settings.bigObjectThreshold = qRound((5 * level + 2) * dpiFactor);
  return settings;
}

struct Component {
  static const uint32_t ANCHORED_TO_BIG = uint32_t(1) << 31;
  static const uint32_t ANCHORED_TO_SMALL = uint32_t(1) << 30;
  static const uint32_t TAG_MASK = ANCHORED_TO_BIG | ANCHORED_TO_SMALL;

  /**
   * Lower 30 bits: the number of pixels in the connected component.
   * Higher 2 bits: tags.
   */
  uint32_t numPixels;

  Component() : numPixels(0) {}

  uint32_t pixelCount() const { return numPixels & ~TAG_MASK; }

  uint32_t anchoredToBig() const { return numPixels & ANCHORED_TO_BIG; }

  void setAnchoredToBig() { numPixels |= ANCHORED_TO_BIG; }

  void setAnchoredToSmall() { numPixels |= ANCHORED_TO_SMALL; }

  bool anchoredToSmallButNotBig() const { return (numPixels & TAG_MASK) == ANCHORED_TO_SMALL; }

  void clearTags() { numPixels &= ~TAG_MASK; }
};

const uint32_t Component::ANCHORED_TO_BIG;
const uint32_t Component::ANCHORED_TO_SMALL;
const uint32_t Component::TAG_MASK;

struct BoundingBox {
  int top;
  int left;
  int bottom;
  int right;

  BoundingBox() {
    top = left = std::numeric_limits<int>::max();
    bottom = right = std::numeric_limits<int>::min();
  }

  int width() const { return right - left + 1; }

  int height() const { return bottom - top + 1; }

  void extend(int x, int y) {
    top = std::min(top, y);
    left = std::min(left, x);
    bottom = std::max(bottom, y);
    right = std::max(right, x);
  }
};

struct Vector {
  int16_t x;
  int16_t y;
};

union Distance {
  Vector vec;
  uint32_t raw;

  static Distance zero() {
    Distance dist{};
    dist.raw = 0;
    return dist;
  }

  static Distance special() {
    Distance dist{};
    dist.vec.x = dist.vec.y = std::numeric_limits<int16_t>::max();
    return dist;
  }

  bool operator==(const Distance& other) const { return raw == other.raw; }

  bool operator!=(const Distance& other) const { return raw != other.raw; }

  // REVIEW-NOTE (2026-10): distance vectors are int16_t -> for padded widths above 32767 px they
  // overflow and distances become wrong. Not fixed because such widths are far beyond normal
  // scans; sqdist() itself uses 64-bit math.
  void reset(int x) {
    vec.x = static_cast<int16_t>(std::numeric_limits<int16_t>::max() - x);
    vec.y = 0;
  }

  uint32_t sqdist() const {
    // In 64 bits: with |x| near 32767, x*x + 4*y*y leaves the int range from |y| > 16384 on.
    const int64_t x = vec.x;
    const int64_t y = vec.y;
    return static_cast<uint32_t>(x * x + VERTICAL_SCALE_SQ * y * y);
  }
};

/**
 * \brief A bidirectional association between two connected components.
 */
struct Connection {
  uint32_t lesserLabel;
  uint32_t greaterLabel;

  Connection(uint32_t lbl1, uint32_t lbl2) {
    if (lbl1 < lbl2) {
      lesserLabel = lbl1;
      greaterLabel = lbl2;
    } else {
      lesserLabel = lbl2;
      greaterLabel = lbl1;
    }
  }

  bool operator==(const Connection& other) const {
    return (lesserLabel == other.lesserLabel) && (greaterLabel == other.greaterLabel);
  }

  struct hash {
    std::size_t operator()(const Connection& connection) const noexcept {
      return std::hash<int>()(connection.lesserLabel) ^ std::hash<int>()(connection.greaterLabel << 1);
    }
  };
};

/**
 * \brief A directional assiciation between two connected components.
 */
struct TargetSourceConn {
  uint32_t target;

  /**< The label of the target connected component. */
  uint32_t source;

  /**< The label of the source connected component. */

  TargetSourceConn(uint32_t tgt, uint32_t src) : target(tgt), source(src) {}

  /**
   * The ordering is by target then source. It's designed to be able
   * to quickly locate all associations involving a specific target.
   */
  bool operator<(const TargetSourceConn& rhs) const {
    if (target < rhs.target) {
      return true;
    } else if (target > rhs.target) {
      return false;
    } else {
      return source < rhs.source;
    }
  }
};

/**
 * \brief If the association didn't exist, create it,
 *        otherwise the minimum distance.
 */
void updateDistance(std::unordered_map<Connection, uint32_t, Connection::hash>& conns,
                    uint32_t label1,
                    uint32_t label2,
                    uint32_t sqdist) {
  using Connections = std::unordered_map<Connection, uint32_t, Connection::hash>;

  const Connection conn(label1, label2);
  auto it(conns.find(conn));
  if (it == conns.end()) {
    conns.insert(Connections::value_type(conn, sqdist));
  } else if (sqdist < it->second) {
    it->second = sqdist;
  }
}

/**
 * \brief Tag the source component with ANCHORED_TO_SMALL, ANCHORED_TO_BIG
 *        or none of the above.
 */
void tagSourceComponent(Component& source, const Component& target, uint32_t sqdist, const Settings& settings) {
  if (source.anchoredToBig()) {
    // No point in setting ANCHORED_TO_SMALL.
    return;
  }

  // Without the tags: either component may have been tagged by an earlier connection.
  const uint32_t sourcePixels = source.pixelCount();
  if (sqdist > uint64_t(sourcePixels) * settings.pixelsToSqDist) {
    // Too far.
    return;
  }

  if (target.pixelCount() >= settings.minRelativeParentWeight * sourcePixels) {
    source.setAnchoredToBig();
  } else {
    source.setAnchoredToSmall();
  }
}

/**
 * Check if the component may be attached to another one.
 * Attaching a component to another one will preserve the component
 * being attached, provided that the one it's attached to is also preserved.
 */
bool canBeAttachedTo(const Component& comp, const Component& target, uint32_t sqdist, const Settings& settings) {
  if (sqdist <= uint64_t(comp.pixelCount()) * settings.pixelsToSqDist) {
    if (target.pixelCount() >= comp.pixelCount() * settings.minRelativeParentWeight) {
      return true;
    }
  }
  return false;
}

/*
 * Squared distance from a pixel to the seed of a neighbour, from the neighbour's vector and squared
 * distance: the vector gets one pixel longer, e.g. (x - 1)^2 = x^2 - 2x + 1 for the left neighbour.
 * 2 * x, not x << 1: x is negative where the seed lies to the left, and shifting that is undefined.
 */
inline uint32_t sqdistViaLeft(const uint32_t leftSqdist, const Distance left) {
  return leftSqdist + 1 - 2 * int(left.vec.x);
}

inline uint32_t sqdistViaRight(const uint32_t rightSqdist, const Distance right) {
  return rightSqdist + 1 + 2 * int(right.vec.x);
}

inline uint32_t sqdistViaTop(const uint32_t topSqdist, const Distance top) {
  return topSqdist + (VERTICAL_SCALE_SQ - 2 * VERTICAL_SCALE_SQ * int(top.vec.y));
}

inline uint32_t sqdistViaBottom(const uint32_t bottomSqdist, const Distance bottom) {
  return bottomSqdist + (VERTICAL_SCALE_SQ + 2 * VERTICAL_SCALE_SQ * int(bottom.vec.y));
}

/**
 * A seed a pixel may take over from a neighbour: the vector from the pixel to that seed,
 * its squared distance, and the neighbour's offset from the pixel.
 */
struct Candidate {
  Distance dist;
  uint32_t sqdist;
  int neighbourOffset;
};

/**
 * The line that voronoi() and voronoiSpecial() are scanning, with the squared distances of this
 * line and of the line scanned before it (above in the top to bottom pass, below in the other).
 *
 * \tparam hasFrozenPixels Pixels whose distance equals \c frozen keep their seed and don't pass
 *         it on (voronoiSpecial()). Without them (voronoi()), the checks are compiled out.
 */
template <bool hasFrozenPixels>
struct PropagationLine {
  const int width;
  const Distance frozen;
  Distance* dist;
  uint32_t* cmap;
  uint32_t* sqdist;
  uint32_t* prevSqdist;

  bool isFrozen(const Distance& distance) const { return hasFrozenPixels && distance == frozen; }

  /**
   * Moves to the next line, \p step is +width or -width. The padding pixels at both ends get
   * distances to seeds far to the right, so they never win against a real seed.
   */
  void startLine(const int step) {
    dist += step;
    cmap += step;
    dist[0].reset(0);
    dist[width - 1].reset(width - 1);
    sqdist[0] = dist[0].sqdist();
    sqdist[width - 1] = dist[width - 1].sqdist();
  }

  void finishLine() { std::swap(sqdist, prevSqdist); }

  Candidate fromLeft(const int x) const {
    Distance left = dist[x - 1];
    const uint32_t leftSqdist = sqdistViaLeft(sqdist[x - 1], left);
    --left.vec.x;
    return {left, leftSqdist, -1};
  }

  Candidate fromRight(const int x) const {
    Distance right = dist[x + 1];
    const uint32_t rightSqdist = sqdistViaRight(sqdist[x + 1], right);
    ++right.vec.x;
    return {right, rightSqdist, 1};
  }

  Candidate fromTop(const int x) const {
    Distance top = dist[x - width];
    const uint32_t topSqdist = sqdistViaTop(prevSqdist[x], top);
    --top.vec.y;
    return {top, topSqdist, -width};
  }

  Candidate fromBottom(const int x) const {
    Distance bottom = dist[x + width];
    const uint32_t bottomSqdist = sqdistViaBottom(prevSqdist[x], bottom);
    ++bottom.vec.y;
    return {bottom, bottomSqdist, width};
  }

  void take(const int x, const Candidate& candidate) {
    sqdist[x] = candidate.sqdist;
    dist[x] = candidate.dist;
    cmap[x] = cmap[x + candidate.neighbourOffset];
  }

  void takeIfNearer(const int x, const Candidate& candidate) {
    if (candidate.sqdist < sqdist[x]) {
      // A label is never replaced by the 0 of the padding.
      assert(cmap[x] == 0 || cmap[x + candidate.neighbourOffset] != 0);
      take(x, candidate);
    }
  }

  // A frozen neighbour passes nothing on.
  void offerLeft(const int x) {
    if (!isFrozen(dist[x - 1])) {
      takeIfNearer(x, fromLeft(x));
    }
  }

  void offerRight(const int x) {
    if (!isFrozen(dist[x + 1])) {
      takeIfNearer(x, fromRight(x));
    }
  }

  void offerTop(const int x) {
    if (!isFrozen(dist[x - width])) {
      takeIfNearer(x, fromTop(x));
    }
  }

  void offerBottom(const int x) {
    if (!isFrozen(dist[x + width])) {
      takeIfNearer(x, fromBottom(x));
    }
  }

  /**
   * First step of voronoi() for a pixel: a seed (a pixel of a component) stays as it is, any other
   * pixel takes the nearer of the seeds of its left and top neighbour, the top one on a tie.
   */
  void seedOrNearerOfLeftAndTop(const int x) {
    if (cmap[x]) {
      sqdist[x] = 0;
      assert(dist[x].raw == 0);  // still the value dist was filled with
      return;
    }
    const Candidate left = fromLeft(x);
    const Candidate top = fromTop(x);
    take(x, left.sqdist < top.sqdist ? left : top);
  }

  /**
   * First step of voronoiSpecial() for a pixel: it keeps its own seed unless the left or top
   * neighbour has a nearer one, the left one on a tie.
   */
  void ownOrNearerOfLeftAndTop(const int x) {
    if (isFrozen(dist[x])) {
      return;
    }
    sqdist[x] = dist[x].sqdist();
    offerLeft(x);
    offerTop(x);
  }
};

/**
 * Spreads the seeds over the distance matrix: each pixel gets the vector to its nearest seed and
 * that seed's label in \p cmap. A pass from top to bottom and one back, each scanning every line
 * from left to right and back.
 *
 * \param topDownEnd The line the top to bottom pass stops before; the pass back starts at the line
 *        above the one it stopped at.
 */
template <bool hasFrozenPixels>
void propagateDistances(ConnectivityMap& cmap,
                        std::vector<Distance>& dist,
                        const int topDownEnd,
                        const Distance frozen) {
  const int width = cmap.size().width() + 2;
  const int height = cmap.size().height() + 2;

  std::vector<uint32_t> sqdists(width * 2, 0);
  PropagationLine<hasFrozenPixels> line{width, frozen, &dist[0], cmap.paddedData(), &sqdists[width], &sqdists[0]};

  // The top padding line: distances to a seed far to the right.
  line.dist[0].reset(0);
  line.prevSqdist[0] = line.dist[0].sqdist();
  for (int x = 1; x < width; ++x) {
    line.dist[x].vec.x = static_cast<int16_t>(line.dist[x - 1].vec.x - 1);
    line.prevSqdist[x] = sqdistViaLeft(line.prevSqdist[x - 1], line.dist[x - 1]);
  }

  // Top to bottom pass.
  for (int y = 1; y < topDownEnd; ++y) {
    line.startLine(width);
    for (int x = 1; x < width - 1; ++x) {
      if constexpr (hasFrozenPixels) {
        line.ownOrNearerOfLeftAndTop(x);
      } else {
        line.seedOrNearerOfLeftAndTop(x);
      }
    }
    for (int x = width - 2; x >= 1; --x) {
      if (!line.isFrozen(line.dist[x])) {
        line.offerRight(x);
      }
    }
    line.finishLine();
  }

  // Bottom to top pass.
  for (int y = height - 2; y >= 1; --y) {
    line.startLine(-width);
    for (int x = width - 2; x >= 1; --x) {
      if (line.isFrozen(line.dist[x])) {
        continue;
      }
      line.sqdist[x] = line.dist[x].sqdist();
      line.offerRight(x);
      line.offerBottom(x);
    }
    for (int x = 1; x < width - 1; ++x) {
      if (!line.isFrozen(line.dist[x])) {
        line.offerLeft(x);
      }
    }
    line.finishLine();
  }
}

void voronoi(ConnectivityMap& cmap, std::vector<Distance>& dist) {
  const int width = cmap.size().width() + 2;
  const int height = cmap.size().height() + 2;

  assert(dist.empty());
  dist.resize(width * height, Distance::zero());

  // Passes through the bottom padding line, so the pass back starts at the last image line.
  propagateDistances<false>(cmap, dist, height, Distance::zero() /* no frozen pixels */);
}

/**
 * Like voronoi(), on the result of it, with the pixels whose distance equals \p specialDistance
 * frozen.
 */
void voronoiSpecial(ConnectivityMap& cmap, std::vector<Distance>& dist, const Distance specialDistance) {
  const int height = cmap.size().height() + 2;

  // Unlike voronoi(), the top to bottom pass stops before the bottom padding line. So the pass back
  // starts one line higher: it leaves out the last image line and goes through the top padding
  // line. Inherited from ScanTailor; kept as it is.
  propagateDistances<true>(cmap, dist, height - 1, specialDistance);
}

/**
 * Calculate the minimum distance between components from neighboring
 * Voronoi segments.
 */
void voronoiDistances(const ConnectivityMap& cmap,
                      const std::vector<Distance>& distanceMatrix,
                      std::unordered_map<Connection, uint32_t, Connection::hash>& conns) {
  const int width = cmap.size().width();
  const int height = cmap.size().height();

  const int offsets[] = {-cmap.stride(), -1, 1, cmap.stride()};

  const uint32_t* const cmapData = cmap.data();
  const Distance* const distanceData = &distanceMatrix[0] + width + 3;
  for (int y = 0, offset = 0; y < height; ++y, offset += 2) {
    for (int x = 0; x < width; ++x, ++offset) {
      const uint32_t label = cmapData[offset];
      assert(label != 0);

      // REVIEW-NOTE (2026-10): the neighbor's seed is computed from the current pixel's position
      // and without VERTICAL_SCALE_SQ -> component distances are off by up to one pixel. Not fixed
      // because it is inherited from ScanTailor and changes despeckling results (see second-chance note).
      const int x1 = x + distanceData[offset].vec.x;
      const int y1 = y + distanceData[offset].vec.y;

      for (int i : offsets) {
        const int nbhOffset = offset + i;
        const uint32_t nbhLabel = cmapData[nbhOffset];
        if ((nbhLabel == 0) || (nbhLabel == label)) {
          // label 0 can be encountered in
          // padding lines.
          continue;
        }

        const int x2 = x + distanceData[nbhOffset].vec.x;
        const int y2 = y + distanceData[nbhOffset].vec.y;
        // In 64 bits and capped: far apart seeds would wrap around to a small distance.
        const int64_t dx = x1 - x2;
        const int64_t dy = y1 - y2;
        const auto sqdist
            = static_cast<uint32_t>(std::min<int64_t>(dx * dx + dy * dy, std::numeric_limits<uint32_t>::max()));

        updateDistance(conns, label, nbhLabel, sqdist);
      }
    }
  }
}  // voronoiDistances

void despeckleImpl(BinaryImage& image, const Settings& settings, const TaskStatus& status, DebugImages* const dbg) {
  ConnectivityMap cmap(image, CONN8);
  if (cmap.maxLabel() == 0) {
    // Completely white image?
    return;
  }

  status.throwIfCancelled();

  std::vector<Component> components(cmap.maxLabel() + 1);
  std::vector<BoundingBox> boundingBoxes(cmap.maxLabel() + 1);

  const int width = image.width();
  const int height = image.height();

  uint32_t* const cmapData = cmap.data();

  // Count the number of pixels and a bounding rect of each component.
  uint32_t* cmapLine = cmapData;
  const int cmapStride = cmap.stride();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t label = cmapLine[x];
      ++components[label].numPixels;
      boundingBoxes[label].extend(x, y);
    }
    cmapLine += cmapStride;
  }

  status.throwIfCancelled();

  // Unify big components into one.
  std::vector<uint32_t> remappingTable(components.size());
  uint32_t unifiedBigComponent = 0;
  uint32_t nextAvailComponent = 1;
  for (uint32_t label = 1; label <= cmap.maxLabel(); ++label) {
    if ((boundingBoxes[label].width() < settings.bigObjectThreshold)
        && (boundingBoxes[label].height() < settings.bigObjectThreshold)) {
      components[nextAvailComponent] = components[label];
      remappingTable[label] = nextAvailComponent;
      ++nextAvailComponent;
    } else {
      if (unifiedBigComponent == 0) {
        unifiedBigComponent = nextAvailComponent;
        ++nextAvailComponent;
        components[unifiedBigComponent] = components[label];
        // Set numPixels to a large value so that canBeAttachedTo()
        // always allows attaching to any such component.
        // Capped below the tag bits, which a page of 2^30 pixels would reach.
        components[unifiedBigComponent].numPixels
            = static_cast<uint32_t>(std::min<uint64_t>(uint64_t(width) * uint64_t(height), ~Component::TAG_MASK));
      }
      remappingTable[label] = unifiedBigComponent;
    }
  }
  components.resize(nextAvailComponent);
  std::vector<BoundingBox>().swap(boundingBoxes);  // We don't need them any more.
  status.throwIfCancelled();

  const uint32_t maxLabel = nextAvailComponent - 1;
  // Remapping individual pixels.
  cmapLine = cmapData;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      cmapLine[x] = remappingTable[cmapLine[x]];
    }
    cmapLine += cmapStride;
  }
  if (dbg) {
    dbg->add(cmap.visualized(), "big_components_unified");
  }

  status.throwIfCancelled();
  // Build a Voronoi diagram.
  std::vector<Distance> distanceMatrix;
  voronoi(cmap, distanceMatrix);
  if (dbg) {
    dbg->add(cmap.visualized(), "voronoi");
  }

  status.throwIfCancelled();

  Distance* const distanceData = &distanceMatrix[0] + width + 3;

  // Now build a bidirectional map of distances between neighboring
  // connected components.

  using Connections = std::unordered_map<Connection, uint32_t, Connection::hash>;  // conn -> sqdist
  Connections conns;

  voronoiDistances(cmap, distanceMatrix, conns);

  status.throwIfCancelled();

  // Tag connected components with ANCHORED_TO_BIG or ANCHORED_TO_SMALL.
  for (const Connections::value_type& pair : conns) {
    const Connection conn(pair.first);
    const uint32_t sqdist = pair.second;
    Component& comp1 = components[conn.lesserLabel];
    Component& comp2 = components[conn.greaterLabel];
    tagSourceComponent(comp1, comp2, sqdist, settings);
    tagSourceComponent(comp2, comp1, sqdist, settings);
  }

  // Prevent it from growing when we compute the Voronoi diagram
  // the second time.
  components[unifiedBigComponent].setAnchoredToBig();

  bool haveAnchoredToSmallButNotBig = false;
  for (const Component& comp : components) {
    if (comp.anchoredToSmallButNotBig()) {
      haveAnchoredToSmallButNotBig = true;
      break;
    }
  }

  if (haveAnchoredToSmallButNotBig) {
    status.throwIfCancelled();

    // Give such components a second chance. Maybe they do have
    // big neighbors, but Voronoi regions from a smaller ones
    // block the path to the bigger ones.
    //
    // REVIEW-NOTE (2026-10): pixels set to specialDistance/reset() keep vectors that point nowhere
    // -> voronoiDistances() reports ~1e9 for links to big components, so the "second chance" never
    // attaches a small component to a big neighbor. Not fixed because it is inherited from
    // ScanTailor and a fix changes despeckling of every page; needs a comparison test first.

    const Distance zeroDistance(Distance::zero());
    const Distance specialDistance(Distance::special());
    for (int y = 0, offset = 0; y < height; ++y, offset += 2) {
      for (int x = 0; x < width; ++x, ++offset) {
        const uint32_t label = cmapData[offset];
        assert(label != 0);

        const Component& comp = components[label];
        if (!comp.anchoredToSmallButNotBig()) {
          if (distanceData[offset] == zeroDistance) {
            // Prevent this region from growing
            // and from being taken over by another
            // by another region.
            distanceData[offset] = specialDistance;
          } else {
            // Allow this region to be taken over by others.
            // Note: x + 1 here is equivalent to x
            // in voronoi() or voronoiSpecial().
            distanceData[offset].reset(x + 1);
          }
        }
      }
    }

    status.throwIfCancelled();

    // Calculate the Voronoi diagram again, but this time
    // treat pixels with a special distance in such a way
    // to prevent them from spreading but also preventing
    // them from being overwritten.
    voronoiSpecial(cmap, distanceMatrix, specialDistance);
    if (dbg) {
      dbg->add(cmap.visualized(), "voronoi_special");
    }

    status.throwIfCancelled();

    // We've got new connections. Add them to the map.
    voronoiDistances(cmap, distanceMatrix, conns);
  }

  status.throwIfCancelled();

  // Clear the distance matrix.
  std::vector<Distance>().swap(distanceMatrix);

  // Remove tags from components.
  for (Component& comp : components) {
    comp.clearTags();
  }
  // Build a directional connection map and only include
  // good connections, that is those with a small enough
  // distance.
  // While at it, clear the bidirectional connection map.
  std::vector<TargetSourceConn> targetSource;
  while (!conns.empty()) {
    const auto it(conns.begin());
    const uint32_t label1 = it->first.lesserLabel;
    const uint32_t label2 = it->first.greaterLabel;
    const uint32_t sqdist = it->second;
    const Component& comp1 = components[label1];
    const Component& comp2 = components[label2];
    if (canBeAttachedTo(comp1, comp2, sqdist, settings)) {
      targetSource.emplace_back(label2, label1);
    }
    if (canBeAttachedTo(comp2, comp1, sqdist, settings)) {
      targetSource.emplace_back(label1, label2);
    }
    conns.erase(it);
  }

  std::sort(targetSource.begin(), targetSource.end());

  status.throwIfCancelled();

  // Create an index for quick access to a group of connections
  // with a specified target.
  std::vector<size_t> targetSourceIdx;
  const size_t numTargetSources = targetSource.size();
  uint32_t prevLabel = uint32_t(0) - 1;
  for (size_t i = 0; i < numTargetSources; ++i) {
    const TargetSourceConn& conn = targetSource[i];
    assert(conn.target != 0);
    for (; prevLabel != conn.target; ++prevLabel) {
      targetSourceIdx.push_back(i);
    }
    assert(targetSourceIdx.size() - 1 == conn.target);
  }
  for (auto label = static_cast<uint32_t>(targetSourceIdx.size()); label <= maxLabel; ++label) {
    targetSourceIdx.push_back(numTargetSources);
  }
  // Labels of components that are to be retained.
  FastQueue<uint32_t> okLabels;
  okLabels.push(unifiedBigComponent);

  while (!okLabels.empty()) {
    const uint32_t label = okLabels.front();
    okLabels.pop();

    Component& comp = components[label];
    if (comp.anchoredToBig()) {
      continue;
    }

    comp.setAnchoredToBig();

    size_t idx = targetSourceIdx[label];
    while (idx < numTargetSources && targetSource[idx].target == label) {
      okLabels.push(targetSource[idx].source);
      ++idx;
    }
  }

  status.throwIfCancelled();
  // Remove unmarked components from the binary image.
  const uint32_t msb = uint32_t(1) << 31;
  uint32_t* imageLine = image.data();
  const int imageStride = image.wordsPerLine();
  cmapLine = cmapData;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!components[cmapLine[x]].anchoredToBig()) {
        imageLine[x >> 5] &= ~(msb >> (x & 31));
      }
    }
    imageLine += imageStride;
    cmapLine += cmapStride;
  }
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
  const Settings settings = Settings::get(level, dpi);
  despeckleImpl(image, settings, status, dbg);
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
  const Settings settings = Settings::get(level, dpi);
  despeckleImpl(image, settings, status, dbg);
}
// Despeckle::despeckleInPlace
