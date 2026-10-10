// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <XmlMarshaller.h>
#include <XmlUnmarshaller.h>

#include <QDomDocument>
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

BOOST_AUTO_TEST_SUITE_END()
