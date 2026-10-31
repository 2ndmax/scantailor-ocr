// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>

#include <QDomDocument>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(FoundationXmlMarshallingTestSuite)

// Characters with a meaning in XML are escaped when the document is written and
// unescaped again when it is read, so XmlUnmarshaller::string() needs no own unescaping.
BOOST_AUTO_TEST_CASE(test_string_round_trip_with_special_characters) {
  const QString original = QStringLiteral(R"(a < b && c > "d" 'e' &amp; ä)");

  QDomDocument doc("test");
  doc.appendChild(XmlMarshaller(doc).string(original, "value"));
  const QString xml = doc.toString();

  QDomDocument readBack;
  BOOST_REQUIRE(static_cast<bool>(readBack.setContent(xml)));
  const QString result = XmlUnmarshaller::string(readBack.documentElement());

  BOOST_CHECK(result == original);
}

// Coordinates from a damaged project file: NaN, infinity or 1e308 in a rectangle, size or line made
// the page geometry built from it NaN, and Qt's polygon clipping crashed on a split line at 1e308
// (found by fuzz_project_geometry). NaN and infinity are read as 0, huge numbers as 1e7.
BOOST_AUTO_TEST_CASE(test_invalid_coordinates_are_limited) {
  QDomDocument doc;
  QDomElement el = doc.createElement("rect");
  el.setAttribute("x", "nan");
  el.setAttribute("y", "-inf");
  el.setAttribute("width", "inf");
  el.setAttribute("height", "12.5");
  BOOST_CHECK(XmlUnmarshaller::rectF(el) == QRectF(0, 0, 0, 12.5));
  BOOST_CHECK(XmlUnmarshaller::sizeF(el) == QSizeF(0, 12.5));
  el.setAttribute("x", "inf");
  el.setAttribute("y", "3");
  BOOST_CHECK(XmlUnmarshaller::pointF(el) == QPointF(0, 3));
  el.setAttribute("x", "1e308");
  el.setAttribute("y", "-1e308");
  BOOST_CHECK(XmlUnmarshaller::pointF(el) == QPointF(1e7, -1e7));
}

BOOST_AUTO_TEST_SUITE_END()
