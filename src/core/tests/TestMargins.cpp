// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <Margins.h>

#include <QDomDocument>
#include <QDomElement>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(CoreMarginsTestSuite)

BOOST_AUTO_TEST_CASE(test_default_constructor) {
  const Margins m;
  BOOST_CHECK(m.left() == 0.0);
  BOOST_CHECK(m.top() == 0.0);
  BOOST_CHECK(m.right() == 0.0);
  BOOST_CHECK(m.bottom() == 0.0);
}

BOOST_AUTO_TEST_CASE(test_value_constructor) {
  const Margins m(1.0, 2.0, 3.0, 4.0);
  BOOST_CHECK(m.left() == 1.0);
  BOOST_CHECK(m.top() == 2.0);
  BOOST_CHECK(m.right() == 3.0);
  BOOST_CHECK(m.bottom() == 4.0);
}

BOOST_AUTO_TEST_CASE(test_setters) {
  Margins m;
  m.setLeft(10.0);
  m.setTop(20.0);
  m.setRight(30.0);
  m.setBottom(40.0);
  BOOST_CHECK(m.left() == 10.0);
  BOOST_CHECK(m.top() == 20.0);
  BOOST_CHECK(m.right() == 30.0);
  BOOST_CHECK(m.bottom() == 40.0);
}

BOOST_AUTO_TEST_CASE(test_xml_roundtrip) {
  const Margins original(1.5, 2.5, 3.5, 4.5);
  QDomDocument doc;
  QDomElement el = original.toXml(doc, "margins");
  doc.appendChild(el);

  const QDomElement readEl = doc.documentElement();
  const Margins fromXml(readEl);
  BOOST_CHECK_CLOSE(fromXml.left(), original.left(), 0.01);
  BOOST_CHECK_CLOSE(fromXml.top(), original.top(), 0.01);
  BOOST_CHECK_CLOSE(fromXml.right(), original.right(), 0.01);
  BOOST_CHECK_CLOSE(fromXml.bottom(), original.bottom(), 0.01);
}

BOOST_AUTO_TEST_CASE(test_from_xml_attributes) {
  QDomDocument doc;
  QDomElement el = doc.createElement("margins");
  el.setAttribute("left", "1.0");
  el.setAttribute("top", "2.0");
  el.setAttribute("right", "3.0");
  el.setAttribute("bottom", "4.0");
  const Margins m(el);
  BOOST_CHECK_CLOSE(m.left(), 1.0, 0.01);
  BOOST_CHECK_CLOSE(m.top(), 2.0, 0.01);
  BOOST_CHECK_CLOSE(m.right(), 3.0, 0.01);
  BOOST_CHECK_CLOSE(m.bottom(), 4.0, 0.01);
}

// A damaged or hand-edited project file must not bring NaN or infinite margins. Negative
// margins are allowed (Margins step, down to -9999).
BOOST_AUTO_TEST_CASE(test_invalid_values_from_xml_become_zero) {
  QDomDocument doc;
  QDomElement el = doc.createElement("margins");
  el.setAttribute("left", "nan");
  el.setAttribute("top", "inf");
  el.setAttribute("right", "-5");
  el.setAttribute("bottom", "7.5x");
  const Margins m(el);
  BOOST_CHECK_EQUAL(m.left(), 0.0);
  BOOST_CHECK_EQUAL(m.top(), 0.0);
  BOOST_CHECK_EQUAL(m.right(), -5.0);
  BOOST_CHECK_EQUAL(m.bottom(), 0.0);
}

// Margins beyond the range of the Margins step (plus or minus 9999 mm) made the page so large that
// its geometry became infinite (found by fuzz_project_geometry).
BOOST_AUTO_TEST_CASE(test_huge_values_from_xml_are_limited) {
  QDomDocument doc;
  QDomElement el = doc.createElement("margins");
  el.setAttribute("left", "1e308");
  el.setAttribute("top", "-1e308");
  el.setAttribute("right", "9999");
  el.setAttribute("bottom", "-10000");
  const Margins m(el);
  BOOST_CHECK_EQUAL(m.left(), 9999.0);
  BOOST_CHECK_EQUAL(m.top(), -9999.0);
  BOOST_CHECK_EQUAL(m.right(), 9999.0);
  BOOST_CHECK_EQUAL(m.bottom(), -9999.0);
}

BOOST_AUTO_TEST_SUITE_END()
