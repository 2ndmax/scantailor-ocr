// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_UTILS_H_
#define SCANTAILOR_PAGE_SPLIT_UTILS_H_

#include <QPolygonF>

#include "ProjectPages.h"
#include "Settings.h"

class DefaultParams;
class ImageTransformation;

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

  /**
   * The area of a page that Split Pages passes on: the page outline, rounded to whole pixels, inside
   * the incoming pre-crop area (see intersectPreCropWithPageOutline()).
   */
  static QPolygonF pageArea(const PageLayout& layout, PageId::SubPage page, const QPolygonF& incomingPreCrop);

  /**
   * Whether every page of p layout keeps at least a pixel of the image of p xform. A split line
   * from a project file may leave a page only a sliver without area; later steps fail on it.
   */
  static bool everyPageHasArea(const PageLayout& layout, const ImageTransformation& xform);

  static ProjectPages::LayoutType toPageLayoutType(const PageLayout& layout);
};
}  // namespace page_split

#endif  // SCANTAILOR_PAGE_SPLIT_UTILS_H_
