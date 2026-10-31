// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include <ImageTransformation.h>
#include <core/DefaultParams.h>

#include <QPainterPath>
#include <cassert>
#include <cmath>
#include <vector>

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

QPolygonF Utils::pageArea(const PageLayout& layout, const PageId::SubPage page, const QPolygonF& incomingPreCrop) {
  return intersectPreCropWithPageOutline(layout.pageOutline(page).toPolygon(), incomingPreCrop);
}

namespace {
double polygonArea(const QPolygonF& poly) {
  double twiceArea = 0.0;
  for (int i = 0; i < poly.size(); ++i) {
    const QPointF& p = poly[i];
    const QPointF& q = poly[(i + 1) % poly.size()];
    twiceArea += p.x() * q.y() - q.x() * p.y();
  }
  return std::abs(twiceArea) / 2.0;
}
}  // namespace

bool Utils::everyPageHasArea(const PageLayout& layout, const ImageTransformation& xform) {
  std::vector<PageId::SubPage> pages{PageId::SINGLE_PAGE};
  if (layout.type() == PageLayout::TWO_PAGES) {
    pages = {PageId::LEFT_PAGE, PageId::RIGHT_PAGE};
  }
  for (const PageId::SubPage page : pages) {
    ImageTransformation pageXform(xform);
    pageXform.setPreCropArea(pageArea(layout, page, xform.preCropArea()));
    // The part of the original image the later steps work on.
    const QPolygonF part = pageXform.origPreCropArea().intersected(QPolygonF(pageXform.origRect()));
    if (polygonArea(part) < 1.0) {
      return false;
    }
  }
  return true;
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
