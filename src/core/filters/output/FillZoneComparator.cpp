// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FillZoneComparator.h"

#include <PolygonUtils.h>

#include "FillColorProperty.h"
#include "OutputZoneOps.h"
#include "ZoneSet.h"

using namespace imageproc;

namespace output {
bool FillZoneComparator::equal(const ZoneSet& lhs, const ZoneSet& rhs) {
  return detail::equalZoneSets(lhs, rhs, [](const Zone& a, const Zone& b) { return FillZoneComparator::equal(a, b); });
}

bool FillZoneComparator::equal(const Zone& lhs, const Zone& rhs) {
  if (!PolygonUtils::fuzzyCompare(lhs.spline().toPolygon(), rhs.spline().toPolygon())) {
    return false;
  }
  return equal(lhs.properties(), rhs.properties());
}

bool FillZoneComparator::equal(const PropertySet& lhs, const PropertySet& rhs) {
  using FCP = FillColorProperty;
  return lhs.locateOrDefault<FCP>()->color() == rhs.locateOrDefault<FCP>()->color();
}
}  // namespace output
