// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DeviationProvider.h>

#include <boost/test/unit_test.hpp>
#include <cmath>

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

BOOST_AUTO_TEST_CASE(test_unchanged_value_does_not_invalidate_statistics) {
  struct ProbeDeviationProvider : DeviationProvider<int> {
    using DeviationProvider<int>::needsUpdate;
  };

  ProbeDeviationProvider provider;
  addAngles(provider);
  BOOST_CHECK_GE(provider.getDeviationValue(0), 0.0);
  BOOST_CHECK(!provider.needsUpdate());

  // Re-adding an identical finite value keeps cached statistics intact.
  provider.addOrUpdate(0, 0.1);
  BOOST_CHECK(!provider.needsUpdate());

  // Identical NaN updates likewise avoid dirtying cached statistics.
  provider.addOrUpdate(99, NAN);
  BOOST_CHECK(provider.needsUpdate());
  BOOST_CHECK_GE(provider.getDeviationValue(0), 0.0);
  BOOST_CHECK(!provider.needsUpdate());
  provider.addOrUpdate(99, NAN);
  BOOST_CHECK(!provider.needsUpdate());

  // A real change still triggers recomputation.
  provider.addOrUpdate(0, 0.2);
  BOOST_CHECK(provider.needsUpdate());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
