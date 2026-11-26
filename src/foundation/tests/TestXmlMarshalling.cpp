// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>

#include <QDomDocument>
#include <QLineF>
#include <QPointF>
#include <QPolygonF>
#include <QRect>
#include <QRectF>
#include <QSize>
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

BOOST_AUTO_TEST_CASE(test_geometry_round_trips) {
  QDomDocument doc;
  XmlMarshaller m(doc);

  const QSize sz(640, 480);
  BOOST_CHECK(XmlUnmarshaller::size(m.size(sz, QStringLiteral("size"))) == sz);

  const QSizeF szf(12.5, 34.25);
  BOOST_CHECK(XmlUnmarshaller::sizeF(m.sizeF(szf, QStringLiteral("sizef"))) == szf);

  const QPointF pt(3.5, -7.25);
  BOOST_CHECK(XmlUnmarshaller::pointF(m.pointF(pt, QStringLiteral("pt"))) == pt);

  const QLineF line(QPointF(1.0, 2.0), QPointF(9.5, -4.5));
  BOOST_CHECK(XmlUnmarshaller::lineF(m.lineF(line, QStringLiteral("line"))) == line);

  const QRect rc(10, 20, 30, 40);
  BOOST_CHECK(XmlUnmarshaller::rect(m.rect(rc, QStringLiteral("rect"))) == rc);

  const QRectF rcf(1.5, 2.5, 30.0, 40.5);
  BOOST_CHECK(XmlUnmarshaller::rectF(m.rectF(rcf, QStringLiteral("rectf"))) == rcf);

  QPolygonF poly;
  poly << QPointF(0.0, 0.0) << QPointF(10.5, 2.0) << QPointF(5.0, 8.25);
  BOOST_CHECK(XmlUnmarshaller::polygonF(m.polygonF(poly, QStringLiteral("poly"))) == poly);
}

BOOST_AUTO_TEST_SUITE_END()
