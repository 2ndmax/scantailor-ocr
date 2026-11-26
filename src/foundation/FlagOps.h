// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_FLAGOPS_H_
#define SCANTAILOR_FOUNDATION_FLAGOPS_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <type_traits>

#define DEFINE_FLAG_OPS(type)                                                      \
  static_assert(std::is_same_v<std::underlying_type_t<type>, unsigned>,            \
                "DEFINE_FLAG_OPS requires an enum with unsigned underlying type"); \
                                                                                   \
  inline type operator&(type lhs, type rhs) {                                      \
    return static_cast<type>(unsigned(lhs) & unsigned(rhs));                       \
  }                                                                                \
                                                                                   \
  inline type operator|(type lhs, type rhs) {                                      \
    return static_cast<type>(unsigned(lhs) | unsigned(rhs));                       \
  }                                                                                \
                                                                                   \
  inline type operator^(type lhs, type rhs) {                                      \
    return static_cast<type>(unsigned(lhs) ^ unsigned(rhs));                       \
  }                                                                                \
                                                                                   \
  inline type operator~(type val) {                                                \
    return static_cast<type>(~unsigned(val));                                      \
  }                                                                                \
                                                                                   \
  inline type& operator&=(type& lhs, type rhs) {                                   \
    lhs = lhs & rhs;                                                               \
    return lhs;                                                                    \
  }                                                                                \
                                                                                   \
  inline type& operator|=(type& lhs, type rhs) {                                   \
    lhs = lhs | rhs;                                                               \
    return lhs;                                                                    \
  }                                                                                \
                                                                                   \
  inline type& operator^=(type& lhs, type rhs) {                                   \
    lhs = lhs ^ rhs;                                                               \
    return lhs;                                                                    \
  }

#endif  // ifndef SCANTAILOR_FOUNDATION_FLAGOPS_H_
