// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <RastLineFinder.h>

#include <QLineF>
#include <QPointF>
#include <boost/test/unit_test.hpp>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace imageproc {
namespace tests {
BOOST_AUTO_TEST_SUITE(RastLineFinderTestSuite)

static bool matchSupportPoints(const std::vector<unsigned>& idxs1, const std::set<unsigned>& idxs2) {
  return std::set<unsigned>(idxs1.begin(), idxs1.end()) == idxs2;
}

BOOST_AUTO_TEST_CASE(test1) {
  // 4- and 3-point lines with min_support_points == 3
  // --------------------------------------------------
  // x     x
  // x x
  // x x
  // x
  // --------------------------------------------------
  std::vector<QPointF> pts;
  pts.emplace_back(-100, -100);
  pts.emplace_back(0, 0);
  pts.emplace_back(100, 100);
  pts.emplace_back(200, 200);
  pts.emplace_back(0, 100);
  pts.emplace_back(100, 0);
  pts.emplace_back(-100, 200);

  std::set<unsigned> line1Idxs;
  line1Idxs.insert(0);
  line1Idxs.insert(1);
  line1Idxs.insert(2);
  line1Idxs.insert(3);

  std::set<unsigned> line2Idxs;
  line2Idxs.insert(4);
  line2Idxs.insert(5);
  line2Idxs.insert(6);

  RastLineFinderParams params;
  params.setMinSupportPoints(3);
  RastLineFinder finder(pts, params);

  std::vector<unsigned> supportIdxs;

  // line 1
  BOOST_REQUIRE(!finder.findNext(&supportIdxs).isNull());
  BOOST_REQUIRE(matchSupportPoints(supportIdxs, line1Idxs));

  // line2
  BOOST_REQUIRE(!finder.findNext(&supportIdxs).isNull());
  BOOST_REQUIRE(matchSupportPoints(supportIdxs, line2Idxs));

  // no more lines
  BOOST_REQUIRE(finder.findNext().isNull());
}

BOOST_AUTO_TEST_CASE(test_nan_parameter_validation) {
  const double qnan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const std::vector<QPointF> pts{{0, 0}, {10, 10}, {20, 20}};

  {
    RastLineFinderParams p;
    p.setAngleToleranceDeg(qnan);
    BOOST_CHECK_THROW(RastLineFinder(pts, p), std::invalid_argument);
  }
  {
    RastLineFinderParams p;
    p.setMaxDistFromLine(qnan);
    BOOST_CHECK_THROW(RastLineFinder(pts, p), std::invalid_argument);
  }
  {
    RastLineFinderParams p;
    p.setAngleRangeDeg(inf, 180.0);
    BOOST_CHECK_THROW(RastLineFinder(pts, p), std::invalid_argument);
  }
  {
    RastLineFinderParams p;
    p.setOrigin(QPointF(qnan, 0.0));
    BOOST_CHECK_THROW(RastLineFinder(pts, p), std::invalid_argument);
  }
}

BOOST_AUTO_TEST_CASE(test_full_360_span_and_strict_dist_threshold) {
  const std::vector<QPointF> pts{{10, 20}, {30, 20}, {50, 20}, {0, 100}, {40, 140}, {80, 180}};

  RastLineFinderParams params;
  params.setAngleRangeDeg(0.0, 360.0);
  params.setAngleToleranceDeg(1.0);
  params.setMaxDistFromLine(0.5);
  params.setMinSupportPoints(3);

  RastLineFinder finder(pts, params);
  std::vector<unsigned> support;
  BOOST_CHECK(!finder.findNext(&support).isNull());
  BOOST_CHECK_EQUAL(support.size(), 3u);
  BOOST_CHECK(!finder.findNext(&support).isNull());
  BOOST_CHECK_EQUAL(support.size(), 3u);
  BOOST_CHECK(finder.findNext().isNull());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace imageproc
