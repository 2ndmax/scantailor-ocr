// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageTrim.h"

#include <algorithm>

namespace fix_orientation {

namespace {

void redistributeExcess(int& leading, int& trailing, const int excess) {
  if (excess <= 0) {
    return;
  }
  const int fromLeading = std::min(leading, (excess + 1) / 2);
  leading -= fromLeading;
  trailing -= (excess - fromLeading);
  leading = std::max(0, leading);
  trailing = std::max(0, trailing);
}

// A quarter turn clockwise moves the left edge to the top, the top edge to the right and so on.
ImageTrim rotatedClockwise(const ImageTrim& trim, const int quarterTurns) {
  ImageTrim result(trim);
  for (int i = 0; i < quarterTurns; ++i) {
    const ImageTrim prev(result);
    result.top = prev.left;
    result.right = prev.top;
    result.bottom = prev.right;
    result.left = prev.bottom;
  }
  return result;
}

}  // namespace

ImageTrim ImageTrim::rotated(const OrthogonalRotation& rotation) const {
  return rotatedClockwise(*this, rotation.toDegrees() / 90);
}

ImageTrim ImageTrim::unrotated(const OrthogonalRotation& rotation) const {
  return rotatedClockwise(*this, (4 - rotation.toDegrees() / 90) % 4);
}

QRect ImageTrim::toInnerRect(const QSize& imageSize) const {
  if (!enabled || imageSize.width() <= 0 || imageSize.height() <= 0) {
    return QRect(0, 0, imageSize.width(), imageSize.height());
  }

  int l = std::max(0, left);
  int t = std::max(0, top);
  int r = std::max(0, right);
  int b = std::max(0, bottom);

  if (l + r + kMinTrimInnerSide > imageSize.width()) {
    redistributeExcess(l, r, l + r + kMinTrimInnerSide - imageSize.width());
  }
  if (t + b + kMinTrimInnerSide > imageSize.height()) {
    redistributeExcess(t, b, t + b + kMinTrimInnerSide - imageSize.height());
  }

  int w = std::max(kMinTrimInnerSide, imageSize.width() - l - r);
  int h = std::max(kMinTrimInnerSide, imageSize.height() - t - b);

  if (l + w > imageSize.width()) {
    l = std::max(0, imageSize.width() - w);
  }
  if (t + h > imageSize.height()) {
    t = std::max(0, imageSize.height() - h);
  }

  return QRect(l, t, w, h);
}

}  // namespace fix_orientation
