// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ArcLengthMapper.h>

#include <boost/test/unit_test.hpp>
#include <cmath>

namespace math {
namespace tests {
BOOST_AUTO_TEST_SUITE(ArcLengthMapperTestSuite)

BOOST_AUTO_TEST_CASE(test_mapping_and_degenerate_segments) {
  ArcLengthMapper mapper;
  mapper.addSample(0.0, 0.0);
  mapper.addSample(3.0, 4.0);
  mapper.addSample(3.0, 10.0);  // non-increasing x ignored safely
  mapper.addSample(6.0, 4.0);

  BOOST_CHECK_CLOSE(mapper.totalArcLength(), 8.0, 1e-9);
  ArcLengthMapper::Hint hint;
  BOOST_CHECK_CLOSE(mapper.xToArcLen(3.0, hint), 5.0, 1e-9);
  BOOST_CHECK_CLOSE(mapper.arcLenToX(5.0, hint), 3.0, 1e-9);

  mapper.normalizeRange(0.0);
  BOOST_CHECK_EQUAL(mapper.totalArcLength(), 0.0);
  const double mappedX = mapper.arcLenToX(1.0, hint);
  BOOST_CHECK(std::isfinite(mappedX));

  ArcLengthMapper tiny;
  tiny.addSample(0.0, 0.0);
  tiny.addSample(1e-200, 1e-200);
  BOOST_CHECK(tiny.totalArcLength() > 0.0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace tests
}  // namespace math
