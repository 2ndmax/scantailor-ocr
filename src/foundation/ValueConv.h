// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_VALUECONV_H_
#define SCANTAILOR_FOUNDATION_VALUECONV_H_

#include <cmath>
#include <limits>

template <typename ToType>
class StaticCastValueConv {
 public:
  template <typename FromType>
  ToType operator()(FromType val) const {
    return static_cast<ToType>(val);
  }
};


template <typename ToType>
class RoundAndClipValueConv {
 public:
  explicit RoundAndClipValueConv(ToType min = std::numeric_limits<ToType>::lowest(),
                                 ToType max = std::numeric_limits<ToType>::max())
      : m_min(min), m_max(max) {}

  template <typename FromType>
  ToType operator()(FromType val) const {
    static_assert(std::numeric_limits<ToType>::is_integer, "RoundAndClipValueConv requires an integer target type");
    static_assert(std::numeric_limits<ToType>::digits <= std::numeric_limits<double>::digits,
                  "RoundAndClipValueConv target type must be exactly representable in double");

    // Compare in double so 32-bit integer bounds are represented exactly (e.g. float(INT32_MAX)
    // rounds up to 2^31, which would otherwise pass a float bounds check and overflow on cast).
    // Written as "not at least the minimum", so that NaN, which fails every comparison, gets the
    // minimum instead of an undefined cast.
    const double dval = static_cast<double>(val);
    if (!(dval >= static_cast<double>(m_min))) {
      return m_min;
    } else if (dval > static_cast<double>(m_max)) {
      return m_max;
    } else {
      return static_cast<ToType>(std::floor(dval + 0.5));
    }
  }

 private:
  ToType m_min;
  ToType m_max;
};


#endif  // ifndef SCANTAILOR_FOUNDATION_VALUECONV_H_
