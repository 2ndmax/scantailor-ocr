// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_UTILS_H_
#define SCANTAILOR_FIX_ORIENTATION_UTILS_H_

class DefaultParams;
class ImageTransformation;
class OrthogonalRotation;

namespace fix_orientation {
struct ImageTrim;

class Utils {
 public:
  Utils() = delete;

  static OrthogonalRotation getDefaultOrthogonalRotation(const DefaultParams& defaultParams);

  /** Sets the rotation and the trim (in source image pixels) on a \p xform without a rotation. */
  static void applyRotationAndTrim(ImageTransformation& xform,
                                   const OrthogonalRotation& rotation,
                                   const ImageTrim& trim);
};
}  // namespace fix_orientation

#endif  // SCANTAILOR_FIX_ORIENTATION_UTILS_H_
