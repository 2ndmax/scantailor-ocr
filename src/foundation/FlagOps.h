// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_FLAGOPS_H_
#define SCANTAILOR_FOUNDATION_FLAGOPS_H_

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

// REVIEW-NOTE (2026-10): operator~ yields values outside the range of enums without a fixed underlying type ->
// undefined behavior since C++17. Not fixed because no code applies ~ to these flags today.
// REVIEW-NOTE (2026-10): an OR of several flags is not itself an enumerator -> clang-analyzer
// optin.core.EnumCastOutOfRange warns at every user of this macro (FittableSpline::SampleFlags,
// PolylineModelShape::Flags, ThumbnailSequence::SelectionFlags, ...). Not fixed because the value range of an enum
// covers all bits of its largest enumerator, so &, | and ^ of its flags always stay inside it.
#define DEFINE_FLAG_OPS(type)                    \
  inline type operator&(type lhs, type rhs) {    \
    return type(unsigned(lhs) & unsigned(rhs));  \
  }                                              \
                                                 \
  inline type operator|(type lhs, type rhs) {    \
    return type(unsigned(lhs) | unsigned(rhs));  \
  }                                              \
                                                 \
  inline type operator^(type lhs, type rhs) {    \
    return type(unsigned(lhs) ^ unsigned(rhs));  \
  }                                              \
                                                 \
  inline type operator~(type val) {              \
    return type(~unsigned(val));                 \
  }                                              \
                                                 \
  inline type& operator&=(type& lhs, type rhs) { \
    lhs = lhs & rhs;                             \
    return lhs;                                  \
  }                                              \
                                                 \
  inline type& operator|=(type& lhs, type rhs) { \
    lhs = lhs | rhs;                             \
    return lhs;                                  \
  }                                              \
                                                 \
  inline type& operator^=(type& lhs, type rhs) { \
    lhs = lhs ^ rhs;                             \
    return lhs;                                  \
  }

#endif  // ifndef SCANTAILOR_FOUNDATION_FLAGOPS_H_
