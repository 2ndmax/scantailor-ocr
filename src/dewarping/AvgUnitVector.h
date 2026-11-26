// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DEWARPING_AVGUNITVECTOR_H_
#define SCANTAILOR_DEWARPING_AVGUNITVECTOR_H_

#include <QLineF>
#include <cmath>
#include <utility>

#include "VecNT.h"

namespace dewarping {
/**
 * \brief The average direction of two lines, as a unit vector.
 *
 * Both lines are normalized first, so a longer line does not outweigh
 * a shorter one. Shared by TextLineTracer and TopBottomEdgeTracer for
 * the direction of the vertical content bounds.
 */
inline Vec2f calcAvgUnitVector(const std::pair<QLineF, QLineF>& bounds) {
  Vec2f v1(bounds.first.p2() - bounds.first.p1());
  const float len1 = std::sqrt(v1.squaredNorm());
  const bool valid1 = len1 > 1e-12f;
  if (valid1) {
    v1 /= len1;
  }

  Vec2f v2(bounds.second.p2() - bounds.second.p1());
  const float len2 = std::sqrt(v2.squaredNorm());
  const bool valid2 = len2 > 1e-12f;
  if (valid2) {
    v2 /= len2;
  }

  if (!valid1 && !valid2) {
    return Vec2f(0.0f, 1.0f);
  }
  if (!valid1) {
    return v2;
  }
  if (!valid2) {
    return v1;
  }

  Vec2f v3(v1 + v2);
  const float len3 = std::sqrt(v3.squaredNorm());
  if (len3 <= 1e-12f) {
    return Vec2f(-v1[1], v1[0]);
  }
  v3 /= len3;
  return v3;
}
}  // namespace dewarping

#endif  // SCANTAILOR_DEWARPING_AVGUNITVECTOR_H_
