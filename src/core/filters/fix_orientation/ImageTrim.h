// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_IMAGETRIM_H_
#define SCANTAILOR_FIX_ORIENTATION_IMAGETRIM_H_

#include <QRect>

#include "OrthogonalRotation.h"

namespace fix_orientation {

inline constexpr int kMinTrimInnerSide = 32;

/**
 * Inward crop from each edge of the source image, in integer source pixels.
 * When disabled, other fields are ignored.
 */
struct ImageTrim {
  bool enabled = false;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;

  QRect toInnerRect(const QSize& imageSize) const;

  /** The edges as seen after rotating the image by \p rotation. */
  ImageTrim rotated(const OrthogonalRotation& rotation) const;

  /** The inverse of rotated(): the edges of the source image. */
  ImageTrim unrotated(const OrthogonalRotation& rotation) const;
};

}  // namespace fix_orientation

#endif
