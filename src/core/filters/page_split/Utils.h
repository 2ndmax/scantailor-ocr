// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_UTILS_H_
#define SCANTAILOR_PAGE_SPLIT_UTILS_H_

#include <QPolygonF>

#include "ProjectPages.h"
#include "Settings.h"

class DefaultParams;

namespace page_split {
class Utils {
 public:
  Utils() = delete;

  static Settings::UpdateAction buildDefaultUpdateAction(const DefaultParams& defaultParams);

  static SplitDetectionMethod defaultDetectionMethod(const DefaultParams& defaultParams);

  /**
   * The part of the page outline that lies inside the incoming pre-crop area.
   * Falls back to the page outline if the two don't overlap.
   */
  static QPolygonF intersectPreCropWithPageOutline(const QPolygonF& pageOutlinePoly, const QPolygonF& incomingPreCrop);

  static ProjectPages::LayoutType toPageLayoutType(const PageLayout& layout);
};
}  // namespace page_split

#endif  // SCANTAILOR_PAGE_SPLIT_UTILS_H_
