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
  v1 /= std::sqrt(v1.squaredNorm());

  Vec2f v2(bounds.second.p2() - bounds.second.p1());
  v2 /= std::sqrt(v2.squaredNorm());

  Vec2f v3(v1 + v2);
  v3 /= std::sqrt(v3.squaredNorm());
  return v3;
}
}  // namespace dewarping

#endif  // SCANTAILOR_DEWARPING_AVGUNITVECTOR_H_
