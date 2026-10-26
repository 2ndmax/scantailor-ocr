// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include <core/DefaultParams.h>

#include <QPainterPath>
#include <cassert>

using namespace page_split;

Settings::UpdateAction Utils::buildDefaultUpdateAction(const DefaultParams& defaultParams) {
  const DefaultParams::PageSplitParams& pageSplitParams = defaultParams.getPageSplitParams();

  Settings::UpdateAction update;
  update.setLayoutType(pageSplitParams.getLayoutType());

  return update;
}

SplitDetectionMethod Utils::defaultDetectionMethod(const DefaultParams& defaultParams) {
  return defaultParams.getPageSplitParams().getDetectionMethod();
}

QPolygonF Utils::intersectPreCropWithPageOutline(const QPolygonF& pageOutlinePoly, const QPolygonF& incomingPreCrop) {
  QPainterPath pathPage;
  pathPage.addPolygon(pageOutlinePoly);
  QPainterPath pathIncoming;
  pathIncoming.addPolygon(incomingPreCrop);
  const QPainterPath intersection = pathPage.intersected(pathIncoming);
  if (intersection.isEmpty()) {
    return pageOutlinePoly;
  }
  const QPolygonF clipped = intersection.toFillPolygon();
  if (clipped.isEmpty()) {
    return pageOutlinePoly;
  }
  return clipped;
}

ProjectPages::LayoutType Utils::toPageLayoutType(const PageLayout& layout) {
  switch (layout.type()) {
    case PageLayout::SINGLE_PAGE_UNCUT:
    case PageLayout::SINGLE_PAGE_CUT:
      return ProjectPages::ONE_PAGE_LAYOUT;
    case PageLayout::TWO_PAGES:
      return ProjectPages::TWO_PAGE_LAYOUT;
  }

  assert(!"Unreachable");
  return ProjectPages::ONE_PAGE_LAYOUT;
}
