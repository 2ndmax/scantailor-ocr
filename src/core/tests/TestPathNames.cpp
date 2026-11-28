// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <PathNames.h>

#include <QChar>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QStringList>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <utility>

namespace {
struct ScopedReadOnlyFile {
  QString path;
  bool applied = false;

  explicit ScopedReadOnlyFile(QString p)
      : path(std::move(p)), applied(QFile::setPermissions(path, QFileDevice::ReadOwner)) {}

  ~ScopedReadOnlyFile() {
    if (applied) {
      QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
  }

  bool ok() const { return applied; }

  ScopedReadOnlyFile(const ScopedReadOnlyFile&) = delete;
  ScopedReadOnlyFile& operator=(const ScopedReadOnlyFile&) = delete;
};
}  // namespace

using path_names::canCreateOrWriteDir;
using path_names::canWriteFile;
using path_names::invalidNameReason;
using path_names::invalidNewPathReason;
using path_names::isWritableDir;

BOOST_AUTO_TEST_SUITE(PathNamesTestSuite)

BOOST_AUTO_TEST_CASE(test_ordinary_names_are_valid) {
  const QStringList names{QStringLiteral("Buch"),
                          QStringLiteral("Buch.ScanTailor"),
                          QStringLiteral(".hidden"),
                          QStringLiteral("two words"),
                          QString::fromUtf8("B\xC3\xBC\x63her"),                           // Bücher
                          QString::fromUtf8("\xD0\x9A\xD0\xBD\xD0\xB8\xD0\xB3\xD0\xB0")};  // Книга
  for (const QString& name : names) {
    BOOST_CHECK(invalidNameReason(name).isEmpty());
  }
}

BOOST_AUTO_TEST_CASE(test_names_that_only_resemble_reserved_ones_are_valid) {
  for (const char* name : {"CON2", "Konsole", "COM10", "LPT", "COM", "nullable.txt", "my.nul", "nul a.txt"}) {
    BOOST_CHECK(invalidNameReason(QString::fromLatin1(name)).isEmpty());
  }
}

BOOST_AUTO_TEST_CASE(test_empty_name_is_rejected) {
  BOOST_CHECK(invalidNameReason(QString()).contains("empty"));
}

BOOST_AUTO_TEST_CASE(test_every_forbidden_character_is_named) {
  for (const QChar character : QStringLiteral("\\/:*?\"<>|")) {
    const QString reason = invalidNameReason(QStringLiteral("a") + character + QStringLiteral("b"));
    BOOST_CHECK(reason.contains(QStringLiteral("the character \"") + character + QLatin1Char('"')));
  }
}

BOOST_AUTO_TEST_CASE(test_control_characters_are_rejected) {
  for (const char16_t code : {u'\x01', u'\t', u'\n', u'\x1F'}) {
    const QString name = QStringLiteral("a") + QChar(code) + QStringLiteral("b");
    BOOST_CHECK(invalidNameReason(name).contains("control character"));
  }
}

BOOST_AUTO_TEST_CASE(test_period_or_space_at_the_end_is_rejected) {
  for (const char* name : {"Buch.", "Buch ", ".", "..", " ", "Buch. "}) {
    BOOST_CHECK(invalidNameReason(QString::fromLatin1(name)).contains("ends with"));
  }
  BOOST_CHECK(invalidNameReason(QStringLiteral(" Buch")).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_reserved_names_are_rejected) {
  for (const char* name : {"CON", "PRN", "AUX", "NUL", "COM1", "COM9", "LPT1", "LPT9", "con", "Nul", "nul.txt",
                           "CON.ScanTailor", "aux.tar.gz", "COM0", "LPT0", "nul .txt", "COM1  .txt"}) {
    BOOST_CHECK(invalidNameReason(QString::fromLatin1(name)).contains("reserved"));
  }
  for (const char* name : {"COM\xC2\xB9", "COM\xC2\xB2", "LPT\xC2\xB3", "com\xC2\xB9.txt"}) {
    BOOST_CHECK(invalidNameReason(QString::fromUtf8(name)).contains("reserved"));
  }
}

BOOST_AUTO_TEST_CASE(test_new_path_with_valid_names_is_accepted) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("new/out")).isEmpty());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("new/out/")).isEmpty());
  BOOST_CHECK(invalidNewPathReason(dir.path()).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_every_new_part_of_a_path_is_judged) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("new./out")).contains("\"new.\""));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("new/out.")).contains("\"out.\""));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("nul/out")).contains("\"nul\""));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("a?b/out")).contains("\"a?b\""));
}

BOOST_AUTO_TEST_CASE(test_dots_that_lead_through_folders_are_not_names) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("new/../other/./out")).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_empty_path_is_rejected) {
  BOOST_CHECK(!invalidNewPathReason(QString()).isEmpty());
  BOOST_CHECK(!canCreateOrWriteDir(QString()));
  BOOST_CHECK(!canWriteFile(QString()));
}

BOOST_AUTO_TEST_CASE(test_reserved_names_are_rejected_by_write_checks) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(!canWriteFile(dir.filePath("CON.txt")));
  BOOST_CHECK(!canCreateOrWriteDir(dir.filePath("NUL/sub")));
}

BOOST_AUTO_TEST_CASE(test_isWritableDir_reports_writable_and_missing_dirs) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(isWritableDir(dir.path()));
  BOOST_CHECK(!isWritableDir(dir.filePath("missing")));
}

BOOST_AUTO_TEST_CASE(test_canCreateOrWriteDir_accepts_existing_and_creatable_dirs) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_CHECK(canCreateOrWriteDir(dir.path()));
  BOOST_CHECK(canCreateOrWriteDir(dir.filePath("missing/sub")));
}

BOOST_AUTO_TEST_CASE(test_canCreateOrWriteDir_rejects_a_file_in_the_path) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString file = dir.filePath("file");
  QFile f(file);
  BOOST_REQUIRE(f.open(QIODevice::WriteOnly));
  f.close();
  BOOST_CHECK(!canCreateOrWriteDir(file));
  BOOST_CHECK(!canCreateOrWriteDir(file + "/sub"));
}

BOOST_AUTO_TEST_CASE(test_canWriteFile) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // A new file in an existing folder, and an existing writable one.
  BOOST_CHECK(canWriteFile(dir.filePath("new.txt")));
  const QString file = dir.filePath("file");
  QFile f(file);
  BOOST_REQUIRE(f.open(QIODevice::WriteOnly));
  f.close();
  BOOST_CHECK(canWriteFile(file));
  // A missing folder, a file in place of the folder, a folder in place of the file.
  BOOST_CHECK(!canWriteFile(dir.filePath("missing/new.txt")));
  BOOST_CHECK(!canWriteFile(file + "/new.txt"));
  BOOST_CHECK(!canWriteFile(dir.path()));
  // A read-only file.
  const ScopedReadOnlyFile readOnly(file);
  BOOST_REQUIRE(readOnly.ok());
  BOOST_CHECK(!canWriteFile(file));
}

// Windows calls "plain." existing if "plain" exists, but the name is still not in the folder.
BOOST_AUTO_TEST_CASE(test_existing_folder_does_not_excuse_a_period_at_the_end) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_REQUIRE(QDir(dir.path()).mkdir("plain"));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("plain")).isEmpty());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("plain.")).contains("ends with"));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("plain./out")).contains("ends with"));
}

#ifndef Q_OS_WIN
// Only other systems can have such a folder; it exists, so it stays usable.
BOOST_AUTO_TEST_CASE(test_existing_folder_with_a_rejected_name_is_accepted) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  BOOST_REQUIRE(QDir(dir.path()).mkdir("a:b"));
  BOOST_CHECK(invalidNewPathReason(dir.filePath("a:b/out")).isEmpty());
  BOOST_CHECK(invalidNewPathReason(dir.filePath("a:b/out:")).contains("\"out:\""));
}
#endif

BOOST_AUTO_TEST_SUITE_END()
