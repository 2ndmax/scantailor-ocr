// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PictureZoneComparator.h"

#include <PolygonUtils.h>

#include "OutputZoneOps.h"
#include "PictureLayerProperty.h"
#include "ZoneSet.h"

using namespace imageproc;

namespace output {
bool PictureZoneComparator::equal(const ZoneSet& lhs, const ZoneSet& rhs) {
  return detail::equalZoneSets(lhs, rhs,
                               [](const Zone& a, const Zone& b) { return PictureZoneComparator::equal(a, b); });
}

bool PictureZoneComparator::equal(const Zone& lhs, const Zone& rhs) {
  // Like FillZoneComparator: points that only differ by rounding errors are the same zone.
  if (!PolygonUtils::fuzzyCompare(lhs.spline().toPolygon(), rhs.spline().toPolygon())) {
    return false;
  }
  return equal(lhs.properties(), rhs.properties());
}

bool PictureZoneComparator::equal(const PropertySet& lhs, const PropertySet& rhs) {
  using PLP = PictureLayerProperty;
  return lhs.locateOrDefault<PLP>()->layer() == rhs.locateOrDefault<PLP>()->layer();
}
}  // namespace output
