// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <Dpi.h>
#include <Margins.h>
#include <Units.h>
#include <UnitsConverter.h>
#include <UnitsListener.h>
#include <UnitsProvider.h>

#include <QString>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(CoreUnitsTestSuite)

BOOST_AUTO_TEST_CASE(test_units_to_string) {
  BOOST_CHECK(unitsToString(PIXELS) == "px");
  BOOST_CHECK(unitsToString(MILLIMETRES) == "mm");
  BOOST_CHECK(unitsToString(CENTIMETRES) == "cm");
  BOOST_CHECK(unitsToString(INCHES) == "in");
}

BOOST_AUTO_TEST_CASE(test_units_from_string) {
  BOOST_CHECK(unitsFromString("px") == PIXELS);
  BOOST_CHECK(unitsFromString("mm") == MILLIMETRES);
  BOOST_CHECK(unitsFromString("cm") == CENTIMETRES);
  BOOST_CHECK(unitsFromString("in") == INCHES);
}

BOOST_AUTO_TEST_CASE(test_units_from_string_defaults_to_mm) {
  BOOST_CHECK(unitsFromString("") == MILLIMETRES);
  BOOST_CHECK(unitsFromString("unknown") == MILLIMETRES);
}

BOOST_AUTO_TEST_CASE(test_units_to_localized_string) {
  QString px = unitsToLocalizedString(PIXELS);
  QString mm = unitsToLocalizedString(MILLIMETRES);
  QString cm = unitsToLocalizedString(CENTIMETRES);
  QString in = unitsToLocalizedString(INCHES);
  BOOST_CHECK(!px.isEmpty());
  BOOST_CHECK(!mm.isEmpty());
  BOOST_CHECK(!cm.isEmpty());
  BOOST_CHECK(!in.isEmpty());
}

// Millimetres, centimetres and inches convert without a resolution; only pixels need one.
BOOST_AUTO_TEST_CASE(test_physical_units_convert_without_dpi) {
  const UnitsConverter converter{Dpi()};
  double x = 25.4;
  double y = 50.8;
  converter.convert(x, y, MILLIMETRES, INCHES);
  BOOST_CHECK_CLOSE(x, 1.0, 1e-9);
  BOOST_CHECK_CLOSE(y, 2.0, 1e-9);
  converter.convert(x, y, INCHES, CENTIMETRES);
  BOOST_CHECK_CLOSE(x, 2.54, 1e-9);
  BOOST_CHECK_CLOSE(y, 5.08, 1e-9);

  double px = 100.0;
  double py = 100.0;
  converter.convert(px, py, PIXELS, MILLIMETRES);
  BOOST_CHECK_EQUAL(px, 100.0);  // Left as it is.
}

// Pixels to millimetres and to inches must agree: 72 dpi is 2834.65 pixels per metre, not 2835.
BOOST_AUTO_TEST_CASE(test_pixel_conversion_is_exact) {
  const UnitsConverter converter{Dpi(72, 72)};
  double x = 1000.0;
  double y = 1000.0;
  converter.convert(x, y, PIXELS, MILLIMETRES);
  BOOST_CHECK_CLOSE(x, 1000.0 / 72.0 * 25.4, 1e-9);
  BOOST_CHECK_CLOSE(y, 1000.0 / 72.0 * 25.4, 1e-9);
  converter.convert(x, y, MILLIMETRES, PIXELS);
  BOOST_CHECK_CLOSE(x, 1000.0, 1e-9);

  x = 1000.0;
  converter.convert(x, y, PIXELS, CENTIMETRES);
  BOOST_CHECK_CLOSE(x, 1000.0 / 72.0 * 2.54, 1e-9);
  converter.convert(x, y, CENTIMETRES, PIXELS);
  BOOST_CHECK_CLOSE(x, 1000.0, 1e-9);
}

namespace {
// Takes itself off the listener list when the units change, as a widget being destroyed in the handler would.
class SelfDeletingListener : public UnitsListener {
 public:
  explicit SelfDeletingListener(int& calls) : m_calls(calls) {}
  void onUnitsChanged(Units /*units*/) override {
    ++m_calls;
    delete this;
  }

 private:
  int& m_calls;
};

class CountingListener : public UnitsListener {
 public:
  explicit CountingListener(int& calls) : m_calls(calls) {}
  void onUnitsChanged(Units /*units*/) override { ++m_calls; }

 private:
  int& m_calls;
};
}  // namespace

BOOST_AUTO_TEST_CASE(test_listener_may_remove_itself_while_notified) {
  UnitsProvider& provider = UnitsProvider::getInstance();
  const Units original = provider.getUnits();
  int calls[3] = {0, 0, 0};
  CountingListener first(calls[0]);
  new SelfDeletingListener(calls[1]);
  CountingListener last(calls[2]);

  provider.setUnits(original == INCHES ? MILLIMETRES : INCHES);
  BOOST_CHECK_EQUAL(calls[0], 1);
  BOOST_CHECK_EQUAL(calls[1], 1);
  BOOST_CHECK_EQUAL(calls[2], 1);

  // The deleted listener is gone for good.
  provider.setUnits(original);
  BOOST_CHECK_EQUAL(calls[0], 2);
  BOOST_CHECK_EQUAL(calls[1], 1);
  BOOST_CHECK_EQUAL(calls[2], 2);
}

BOOST_AUTO_TEST_CASE(test_profile_units_follow_the_program_unless_pixels_are_involved) {
  BOOST_CHECK(DefaultParams::displayUnits(MILLIMETRES, INCHES) == INCHES);
  BOOST_CHECK(DefaultParams::displayUnits(MILLIMETRES, CENTIMETRES) == CENTIMETRES);
  BOOST_CHECK(DefaultParams::displayUnits(INCHES, MILLIMETRES) == MILLIMETRES);
  BOOST_CHECK(DefaultParams::displayUnits(MILLIMETRES, PIXELS) == MILLIMETRES);
  BOOST_CHECK(DefaultParams::displayUnits(PIXELS, INCHES) == PIXELS);
}

// A profile shown in another unit keeps its lengths: 25.4 mm are one inch, not 25.4 inches.
BOOST_AUTO_TEST_CASE(test_profile_lengths_are_converted_with_their_unit) {
  DefaultParams profile;
  DefaultParams::SelectContentParams selectContent(profile.getSelectContentParams());
  selectContent.setPageRectSize(QSizeF(254, 508));
  profile.setSelectContentParams(selectContent);
  DefaultParams::PageLayoutParams pageLayout(profile.getPageLayoutParams());
  pageLayout.setHardMargins(Margins(25.4, 50.8, 76.2, 101.6));
  page_layout::OutputSizeOptions outputSize(pageLayout.getOutputSizeOptions());
  outputSize.setTargetSizeMM(QSizeF(127, 254));
  pageLayout.setOutputSizeOptions(outputSize);
  profile.setPageLayoutParams(pageLayout);

  const DefaultParams inches = profile.inUnits(INCHES);

  BOOST_CHECK(inches.getUnits() == INCHES);
  BOOST_CHECK_CLOSE(inches.getSelectContentParams().getPageRectSize().width(), 10.0, 1e-9);
  BOOST_CHECK_CLOSE(inches.getSelectContentParams().getPageRectSize().height(), 20.0, 1e-9);
  const Margins& margins = inches.getPageLayoutParams().getHardMargins();
  BOOST_CHECK_CLOSE(margins.left(), 1.0, 1e-9);
  BOOST_CHECK_CLOSE(margins.top(), 2.0, 1e-9);
  BOOST_CHECK_CLOSE(margins.right(), 3.0, 1e-9);
  BOOST_CHECK_CLOSE(margins.bottom(), 4.0, 1e-9);
  const QSizeF target = inches.getPageLayoutParams().getOutputSizeOptions().targetSizeMM();
  BOOST_CHECK_CLOSE(target.width(), 5.0, 1e-9);
  BOOST_CHECK_CLOSE(target.height(), 10.0, 1e-9);
  // The profile itself is unchanged.
  BOOST_CHECK(profile.getUnits() == MILLIMETRES);
  BOOST_CHECK_CLOSE(profile.getPageLayoutParams().getHardMargins().left(), 25.4, 1e-9);
}

BOOST_AUTO_TEST_SUITE_END()
