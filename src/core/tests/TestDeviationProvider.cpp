// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DeviationProvider.h>

#include <boost/test/unit_test.hpp>

namespace Tests {

BOOST_AUTO_TEST_SUITE(DeviationProviderTestSuite)

namespace {
// Angles around zero, as the deskew step has them. The last one is off by 0.16 degrees.
void addAngles(DeviationProvider<int>& provider) {
  const double angles[] = {0.1, 0.1, 0.1, 0.1, 0.3};
  for (int i = 0; i < 5; ++i) {
    provider.addOrUpdate(i, angles[i]);
  }
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_relative_threshold) {
  DeviationProvider<int> provider;
  addAngles(provider);
  // 1 % of a mean close to zero is no threshold at all.
  BOOST_CHECK(provider.isDeviant(4, 1.5, 1.0));
  BOOST_CHECK(!provider.isDeviant(0, 1.5, 1.0));
}

BOOST_AUTO_TEST_CASE(test_absolute_threshold) {
  DeviationProvider<int> provider;
  provider.setAbsoluteThreshold(true);
  addAngles(provider);
  // Less than one degree off the mean.
  BOOST_CHECK(!provider.isDeviant(4, 1.5, 1.0));
  BOOST_CHECK(provider.isDeviant(4, 1.5, 0.1));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
