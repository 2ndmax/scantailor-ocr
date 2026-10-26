// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_MATH_SEGMENTSEARCH_H_
#define SCANTAILOR_MATH_SEGMENTSEARCH_H_

/**
 * \brief Remembers the segment found last and the direction of travel.
 *
 * Consecutive searches usually end in the same or an adjacent segment,
 * so the next search probes those first. Shared by ArcLengthMapper and
 * PolylineIntersector.
 */
class SegmentHint {
  friend class ArcLengthMapper;
  friend class PolylineIntersector;

 public:
  SegmentHint() = default;

 private:
  void update(const int newSegment) {
    // Note: staying in the same segment tells us nothing about the direction
    // we are scanning in, so the previous one has to be preserved. Resetting
    // it to +1 here would make the "adjacent segment" probes of a backwards
    // scan look the wrong way and fall back to a binary search.
    if (newSegment != m_lastSegment) {
      m_direction = (newSegment < m_lastSegment) ? -1 : 1;
    }
    m_lastSegment = newSegment;
  }

  int m_lastSegment = 0;
  int m_direction = 1;
};

/**
 * \brief Finds the segment [i, i + 1] whose end points lie on different sides
 *        of the searched value, using a binary search.
 *
 * \p signedValue(i) is the signed difference between point \p i and the searched
 * value. The caller makes sure that signedValue(0) and signedValue(numPoints - 1)
 * differ in sign or one of them is zero.
 *
 * \return The index i of the first point of the segment.
 */
template <typename SignedValue>
int findSegmentBinary(const int numPoints, const SignedValue& signedValue) {
  int leftIdx = 0;
  int rightIdx = numPoints - 1;
  double leftValue = signedValue(leftIdx);
  while (leftIdx + 1 < rightIdx) {
    const int midIdx = (leftIdx + rightIdx) >> 1;
    const double midValue = signedValue(midIdx);
    if (midValue * leftValue <= 0) {
      // Note: <= 0 vs < 0 is actually important for this branch.
      // 0 would indicate either left or mid point is our exact answer.
      rightIdx = midIdx;
    } else {
      leftIdx = midIdx;
      leftValue = midValue;
    }
  }
  return leftIdx;
}

#endif  // ifndef SCANTAILOR_MATH_SEGMENTSEARCH_H_
