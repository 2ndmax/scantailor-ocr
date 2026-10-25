// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_UTILS_H_
#define SCANTAILOR_SELECT_CONTENT_UTILS_H_

class DefaultParams;
class Dpi;

namespace select_content {
class Params;

class Utils {
 public:
  Utils() = delete;

  static Params buildDefaultParams(const Dpi& dpi, const DefaultParams& defaultParams);

  /**
   * The resolution of the virtual pixels the page box is measured in: an image with unequal
   * resolutions is scaled to the smaller one (see ImageTransformation::preScaleToEqualizeDpi()).
   */
  static Dpi virtualDpi(const Dpi& imageDpi);
};
}  // namespace select_content

#endif  // SCANTAILOR_SELECT_CONTENT_UTILS_H_
