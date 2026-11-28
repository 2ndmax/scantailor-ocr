// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "MaxWhitespaceFinder.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

namespace imageproc {
using namespace max_whitespace_finder;

namespace {
class AreaCompare {
 public:
  bool operator()(const QRect lhs, const QRect rhs) const {
    const int64_t lhsArea = static_cast<int64_t>(lhs.width()) * lhs.height();
    const int64_t rhsArea = static_cast<int64_t>(rhs.width()) * rhs.height();
    return lhsArea < rhsArea;
  }
};

QRect computeMiddleRect(const QRect& outerRect, const QRect& innerRect) {
  const int deltaLeft = innerRect.left() - outerRect.left();
  const int deltaRight = outerRect.right() - innerRect.right();
  const int deltaTop = innerRect.top() - outerRect.top();
  const int deltaBottom = outerRect.bottom() - innerRect.bottom();

  QRect middleRect(outerRect.left() + ((deltaLeft + 1) >> 1), outerRect.top() + ((deltaTop + 1) >> 1), 0, 0);
  middleRect.setRight(outerRect.right() - (deltaRight >> 1));
  middleRect.setBottom(outerRect.bottom() - (deltaBottom >> 1));
  assert(outerRect.contains(middleRect));
  assert(middleRect.contains(innerRect));
  return middleRect;
}
}  // anonymous namespace

MaxWhitespaceFinder::MaxWhitespaceFinder(const BinaryImage& img, QSize minSize)
    : m_integralImg(img.size()),
      m_queuedRegions(std::make_unique<PriorityStorageImpl<AreaCompare>>(AreaCompare())),
      m_minSize(std::max(1, minSize.width()), std::max(1, minSize.height())) {
  init(img);
}

void MaxWhitespaceFinder::init(const BinaryImage& img) {
  if (img.isNull() || (img.width() < m_minSize.width()) || (img.height() < m_minSize.height())) {
    return;
  }

  const int width = img.width();
  const int height = img.height();
  const uint32_t* line = img.data();
  const int wpl = img.wordsPerLine();

  for (int y = 0; y < height; ++y, line += wpl) {
    m_integralImg.beginRow();
    for (int x = 0; x < width; ++x) {
      const int shift = 31 - (x & 31);
      m_integralImg.push((line[x >> 5] >> shift) & 1);
    }
  }

  Region region(0, img.rect());
  m_queuedRegions->push(region);
}

void MaxWhitespaceFinder::addObstacle(const QRect& obstacle) {
  if (m_queuedRegions->size() == 1) {
    m_queuedRegions->top().addObstacle(obstacle);
  } else {
    m_newObstacles.push_back(obstacle);
  }
}

QRect MaxWhitespaceFinder::next(const ObstacleMode obstacleMode, int maxIterations) {
  while (maxIterations-- > 0 && !m_queuedRegions->empty()) {
    Region& topRegion = m_queuedRegions->top();
    Region region(topRegion);
    region.swapObstacles(topRegion);
    m_queuedRegions->pop();

    region.addNewObstacles(m_newObstacles);

    if (!region.obstacles().empty()) {
      subdivideUsingObstacles(region);
      continue;
    }

    if (m_integralImg.sum(region.bounds()) != 0) {
      subdivideUsingRaster(region);
      continue;
    }

    if (obstacleMode == AUTO_OBSTACLES) {
      m_newObstacles.push_back(region.bounds());
    }
    return region.bounds();
  }
  return QRect();
}

void MaxWhitespaceFinder::subdivideUsingObstacles(const Region& region) {
  const QRect bounds(region.bounds());
  const QRect pivotRect(findPivotObstacle(region));

  subdivide(region, bounds, pivotRect);
}

void MaxWhitespaceFinder::subdivideUsingRaster(const Region& region) {
  const QRect bounds(region.bounds());
  const QPoint pivotPixel(findBlackPixelCloseToCenter(bounds));
  const QRect pivotRect(extendBlackPixelToBlackBox(pivotPixel, bounds));

  subdivide(region, bounds, pivotRect);
}

void MaxWhitespaceFinder::pushSubRegion(const Region& parentRegion, const QRect& bounds) {
  Region newRegion(static_cast<unsigned int>(m_newObstacles.size()), bounds);
  newRegion.addObstacles(parentRegion);
  m_queuedRegions->push(newRegion);
}

void MaxWhitespaceFinder::subdivide(const Region& region, const QRect bounds, const QRect pivot) {
  // Area above the pivot obstacle.
  if (pivot.top() - bounds.top() >= m_minSize.height()) {
    QRect newBounds(bounds);
    newBounds.setBottom(pivot.top() - 1);  // Bottom is inclusive.
    pushSubRegion(region, newBounds);
  }

  // Area below the pivot obstacle.
  if (bounds.bottom() - pivot.bottom() >= m_minSize.height()) {
    QRect newBounds(bounds);
    newBounds.setTop(pivot.bottom() + 1);
    pushSubRegion(region, newBounds);
  }

  // Area to the left of the pivot obstacle.
  if (pivot.left() - bounds.left() >= m_minSize.width()) {
    QRect newBounds(bounds);
    newBounds.setRight(pivot.left() - 1);  // Right is inclusive.
    pushSubRegion(region, newBounds);
  }
  // Area to the right of the pivot obstacle.
  if (bounds.right() - pivot.right() >= m_minSize.width()) {
    QRect newBounds(bounds);
    newBounds.setLeft(pivot.right() + 1);
    pushSubRegion(region, newBounds);
  }
}  // MaxWhitespaceFinder::subdivide

QRect MaxWhitespaceFinder::findPivotObstacle(const Region& region) const {
  assert(!region.obstacles().empty());

  const QPoint center(region.bounds().center());

  QRect bestObstacle;
  int64_t bestDistance = std::numeric_limits<int64_t>::max();
  for (const QRect& obstacle : region.obstacles()) {
    const QPoint vec(center - obstacle.center());
    const int64_t dx = vec.x();
    const int64_t dy = vec.y();
    const int64_t distance = dx * dx + dy * dy;
    if (distance <= bestDistance) {
      bestObstacle = obstacle;
      bestDistance = distance;
    }
  }
  return bestObstacle;
}

void MaxWhitespaceFinder::narrowToBlackBoundary(QRect& outerRect, QRect& innerRect) const {
  // We have two rectangles: the outer one, that always contains at least
  // one black pixel, and the inner one (contained within the outer one),
  // that doesn't contain any black pixels.

  // The first thing we do is bringing those two rectangles as close
  // as possible to each other, so that no more than 1 pixel separates
  // their corresponding edges.
  while (true) {
    const int outerInnerDw = outerRect.width() - innerRect.width();
    const int outerInnerDh = outerRect.height() - innerRect.height();

    if ((outerInnerDw <= 1) && (outerInnerDh <= 1)) {
      break;
    }

    const QRect middleRect(computeMiddleRect(outerRect, innerRect));
    if (m_integralImg.sum(middleRect) == 0) {
      innerRect = middleRect;
    } else {
      outerRect = middleRect;
    }
  }
}

bool MaxWhitespaceFinder::tryResolveEdgeBlackPixel(const QRect& edgeRect,
                                                   const QPoint onHit,
                                                   const QPoint onMiss,
                                                   const bool singlePixelSpan,
                                                   QPoint& result) const {
  const unsigned sum = m_integralImg.sum(edgeRect);
  if (singlePixelSpan) {
    // When dealing with a 1-pixel line, we only have to check at most two endpoints,
    // and at least one of them is black.
    result = (sum != 0) ? onHit : onMiss;
    return true;
  }
  if (sum != 0) {
    result = findBlackPixelCloseToCenter(edgeRect);
    return true;
  }
  return false;
}

QPoint MaxWhitespaceFinder::findBlackPixelCloseToCenter(const QRect nonWhiteRect) const {
  assert(m_integralImg.sum(nonWhiteRect) != 0);

  const QPoint center(nonWhiteRect.center());
  QRect outerRect(nonWhiteRect);
  QRect innerRect(center.x(), center.y(), 1, 1);

  if (m_integralImg.sum(innerRect) != 0) {
    return center;
  }

  narrowToBlackBoundary(outerRect, innerRect);

  QPoint result;
  // Process the left edge.
  if (outerRect.left() != innerRect.left()) {
    QRect rect(outerRect);
    rect.setRight(rect.left());  // Right is inclusive.
    if (tryResolveEdgeBlackPixel(rect, outerRect.topLeft(), outerRect.topRight(), outerRect.height() == 1, result)) {
      return result;
    }
  }
  // Process the right edge.
  if (outerRect.right() != innerRect.right()) {
    QRect rect(outerRect);
    rect.setLeft(rect.right());  // Right is inclusive.
    if (tryResolveEdgeBlackPixel(rect, outerRect.topRight(), outerRect.topLeft(), outerRect.height() == 1, result)) {
      return result;
    }
  }

  // Process the top edge.
  if (outerRect.top() != innerRect.top()) {
    QRect rect(outerRect);
    rect.setBottom(rect.top());  // Bottom is inclusive.
    if (tryResolveEdgeBlackPixel(rect, outerRect.topLeft(), outerRect.bottomLeft(), outerRect.width() == 1, result)) {
      return result;
    }
  }
  // Process the bottom edge.
  assert(outerRect.bottom() != innerRect.bottom());
  QRect rect(outerRect);
  rect.setTop(rect.bottom());  // Bottom is inclusive.
  assert(m_integralImg.sum(rect) != 0);
  if (outerRect.width() == 1) {
    return outerRect.bottomLeft();
  }
  return findBlackPixelCloseToCenter(rect);
}  // MaxWhitespaceFinder::findBlackPixelCloseToCenter

QRect MaxWhitespaceFinder::extendBlackPixelToBlackBox(const QPoint pixel, const QRect bounds) const {
  assert(bounds.contains(pixel));

  QRect outerRect(bounds);
  QRect innerRect(pixel.x(), pixel.y(), 1, 1);

  if (m_integralImg.sum(outerRect) == static_cast<uint64_t>(outerRect.width()) * outerRect.height()) {
    return outerRect;
  }

  // We have two rectangles: the outer one, that always contains at least
  // one white pixel, and the inner one (contained within the outer one),
  // that doesn't.

  // We will be bringing those two rectangles as close as possible to
  // each other, so that no more than 1 pixel separates their
  // corresponding edges.

  while (true) {
    const int outerInnerDw = outerRect.width() - innerRect.width();
    const int outerInnerDh = outerRect.height() - innerRect.height();

    if ((outerInnerDw <= 1) && (outerInnerDh <= 1)) {
      break;
    }

    const QRect middleRect(computeMiddleRect(outerRect, innerRect));
    const uint64_t area = static_cast<uint64_t>(middleRect.width()) * middleRect.height();
    if (m_integralImg.sum(middleRect) == area) {
      innerRect = middleRect;
    } else {
      outerRect = middleRect;
    }
  }
  return innerRect;
}  // MaxWhitespaceFinder::extendBlackPixelToBlackBox

/*======================= MaxWhitespaceFinder::Region =====================*/

MaxWhitespaceFinder::Region::Region(unsigned knownNewObstacles, const QRect& bounds)
    : m_knownNewObstacles(knownNewObstacles), m_bounds(bounds) {}

MaxWhitespaceFinder::Region::Region(const Region& other)
    : m_knownNewObstacles(other.m_knownNewObstacles), m_bounds(other.m_bounds) {
  // Note that we don't copy m_obstacles. This is a shallow copy.
}

/**
 * Adds obstacles from another region that intersect this region's area.
 */
void MaxWhitespaceFinder::Region::addObstacles(const Region& otherRegion) {
  for (const QRect& obstacle : otherRegion.obstacles()) {
    const QRect intersected(obstacle.intersected(m_bounds));
    if (!intersected.isEmpty()) {
      m_obstacles.push_back(intersected);
    }
  }
}

/**
 * Adds global obstacles that were not there when this region was constructed.
 */
void MaxWhitespaceFinder::Region::addNewObstacles(const std::vector<QRect>& newObstacles) {
  for (size_t i = m_knownNewObstacles; i < newObstacles.size(); ++i) {
    const QRect intersected(newObstacles[i].intersected(m_bounds));
    if (!intersected.isEmpty()) {
      m_obstacles.push_back(intersected);
    }
  }
}

/**
 * A fast and non-throwing swap operation.
 */
void MaxWhitespaceFinder::Region::swap(Region& other) {
  std::swap(m_bounds, other.m_bounds);
  std::swap(m_knownNewObstacles, other.m_knownNewObstacles);
  m_obstacles.swap(other.m_obstacles);
}
}  // namespace imageproc
