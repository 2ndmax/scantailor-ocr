// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <OcrLanguages.h>
#include <TessdataDownloader.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

namespace {
bool writeFile(const QString& path, const QByteArray& content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && (file.write(content) == content.size());
}

/** Points TESSDATA_PREFIX to a folder for the lifetime of the object. */
class TessdataPrefix {
 public:
  explicit TessdataPrefix(const QString& dir)
      : m_old(qgetenv("TESSDATA_PREFIX")), m_hadOld(qEnvironmentVariableIsSet("TESSDATA_PREFIX")) {
    qputenv("TESSDATA_PREFIX", QFile::encodeName(dir));
  }

  ~TessdataPrefix() {
    if (m_hadOld) {
      qputenv("TESSDATA_PREFIX", m_old);
    } else {
      qunsetenv("TESSDATA_PREFIX");
    }
  }

 private:
  QByteArray m_old;
  bool m_hadOld;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(OcrLanguagesTestSuite)

BOOST_AUTO_TEST_CASE(test_git_blob_sha1) {
  // The same as "echo hello | git hash-object --stdin".
  BOOST_CHECK(TessdataDownloader::gitBlobSha1("hello\n") == "ce013625030ba8dba906f756967f9e9ca394464a");
  BOOST_CHECK(TessdataDownloader::gitBlobSha1("") == "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391");
}

BOOST_AUTO_TEST_CASE(test_display_name) {
  BOOST_CHECK(OcrLanguages::isScript("script/Latin"));
  BOOST_CHECK(!OcrLanguages::isScript("deu"));
  BOOST_CHECK(OcrLanguages::displayName("script/Latin").startsWith("Latin"));
  // Unknown codes are shown as they are.
  BOOST_CHECK(OcrLanguages::displayName("xyz_abc") == "xyz_abc");
  // Historical languages Qt doesn't know get a name of their own.
  BOOST_CHECK(OcrLanguages::displayName("enm") == "Middle English (enm)");
  BOOST_CHECK(OcrLanguages::displayName("nor") == "Norsk (nor)");
  BOOST_CHECK(!OcrLanguages::isRecognitionModel("equ"));
  BOOST_CHECK(!OcrLanguages::isRecognitionModel("osd"));
  BOOST_CHECK(OcrLanguages::isRecognitionModel("deu"));
}

BOOST_AUTO_TEST_CASE(test_available_and_common_dir) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_REQUIRE(writeFile(dir.filePath("aaa.traineddata"), "a"));
  BOOST_REQUIRE(writeFile(dir.filePath("bbb.traineddata"), "b"));
  BOOST_REQUIRE(writeFile(dir.filePath("osd.traineddata"), "o"));
  BOOST_REQUIRE(writeFile(dir.filePath("equ.traineddata"), "e"));
  BOOST_REQUIRE(writeFile(dir.filePath("script/Latin.traineddata"), "l"));
  const TessdataPrefix prefix(dir.path());

  const std::map<QString, QString> languages = OcrLanguages::available();
  BOOST_CHECK(languages.count("aaa") == 1);
  BOOST_CHECK(languages.count("bbb") == 1);
  BOOST_CHECK(languages.count("script/Latin") == 1);
  BOOST_CHECK(languages.count("osd") == 0);
  BOOST_CHECK(languages.count("equ") == 0);

  const QString common = OcrLanguages::commonDir(QStringList{"aaa", "bbb", "script/Latin"});
  BOOST_CHECK(QDir(common) == QDir(dir.path()));
  BOOST_CHECK(OcrLanguages::commonDir(QStringList{"aaa", "missing"}).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_gather_in_user_dir) {
  // Keep away from the real user folders.
  QStandardPaths::setTestModeEnabled(true);
  const QString userDir = OcrLanguages::userDir();
  if (userDir.isEmpty() || QDir(userDir).exists()) {
    BOOST_TEST_MESSAGE("no unused test user folder, skipped");
    QStandardPaths::setTestModeEnabled(false);
    return;
  }

  QTemporaryDir dir;
  BOOST_REQUIRE(writeFile(dir.filePath("aaa.traineddata"), "content of aaa"));
  BOOST_REQUIRE(writeFile(OcrLanguages::filePath(userDir, "ccc"), "c"));
  const TessdataPrefix prefix(dir.path());

  // "aaa" and "ccc" are in different folders, so "aaa" is copied next to "ccc".
  BOOST_CHECK(OcrLanguages::commonDir(QStringList{"aaa", "ccc"}).isEmpty());
  QString error;
  const QString gathered = OcrLanguages::gatherInUserDir(QStringList{"aaa", "ccc"}, &error);
  BOOST_CHECK(QDir(gathered) == QDir(userDir));
  BOOST_CHECK(error.isEmpty());
  QFile copy(OcrLanguages::filePath(userDir, "aaa"));
  BOOST_REQUIRE(copy.open(QIODevice::ReadOnly));
  BOOST_CHECK(copy.readAll() == "content of aaa");
  copy.close();
  BOOST_CHECK(!QFile::exists(OcrLanguages::filePath(userDir, "aaa") + ".part"));
  BOOST_CHECK(!OcrLanguages::commonDir(QStringList{"aaa", "ccc"}).isEmpty());

  // A language that doesn't exist anywhere.
  BOOST_CHECK(OcrLanguages::gatherInUserDir(QStringList{"missing"}, &error).isEmpty());
  BOOST_CHECK(!error.isEmpty());

  QDir(userDir).removeRecursively();
  QStandardPaths::setTestModeEnabled(false);
}

BOOST_AUTO_TEST_SUITE_END()
