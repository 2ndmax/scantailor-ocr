// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <OcrEngine.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

BOOST_AUTO_TEST_SUITE(OcrEngineTestSuite)

BOOST_AUTO_TEST_CASE(language_files_are_read_from_folders_with_any_characters) {
  // A Cyrillic folder name, as with a Cyrillic user name: outside the ANSI code page of a German
  // Windows, in which Tesseract would open the file itself.
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QDir dir(QDir(tempDir.path()).filePath(QString::fromUtf16(u"Тест")));
  BOOST_REQUIRE(QDir().mkpath(dir.path()));
  const QByteArray content("content of the language file");
  QFile file(dir.filePath("xyz.traineddata"));
  BOOST_REQUIRE(file.open(QIODevice::WriteOnly) && (file.write(content) == content.size()));
  file.close();

  std::vector<char> data;
  BOOST_REQUIRE(OcrEngine::readDataFile(dir.filePath("xyz.traineddata").toUtf8().constData(), &data));
  BOOST_CHECK(QByteArray(data.data(), static_cast<int>(data.size())) == content);
}

BOOST_AUTO_TEST_CASE(a_missing_language_file_is_reported) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  std::vector<char> data;
  BOOST_CHECK(!OcrEngine::readDataFile(QDir(tempDir.path()).filePath("xyz.traineddata").toUtf8().constData(), &data));

  OcrEngine engine;
  QString error;
  BOOST_CHECK(!engine.init(tempDir.path(), "xyz", &error));
  BOOST_CHECK(!error.isEmpty());
}

BOOST_AUTO_TEST_SUITE_END()
