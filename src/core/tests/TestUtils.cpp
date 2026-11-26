// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <CoreUtils.h>

#include <SafeDeletingQObjectPtr.h>

#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#endif

#include "TestHelpers.h"

BOOST_AUTO_TEST_SUITE(CoreUtilsTestSuite)

BOOST_AUTO_TEST_CASE(test_overwriting_rename) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString source = dir.filePath("source.txt");
  const QString target = dir.filePath("target.txt");
  BOOST_REQUIRE(Tests::writeFile(source, "new"));
  BOOST_REQUIRE(Tests::writeFile(target, "old"));

  BOOST_REQUIRE(core::CoreUtils::overwritingRename(source, target));
  BOOST_CHECK(!QFile::exists(source));
  BOOST_CHECK(Tests::readFile(target) == "new");
}

BOOST_AUTO_TEST_CASE(test_overwriting_rename_long_path) {
  // Paths longer than MAX_PATH (260 characters), such as thumbnails of pages whose
  // names grew with each scan inserted after them. Windows refused to replace them.
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString longDir = dir.filePath(QString(200, QChar('d')));
  BOOST_REQUIRE(QDir().mkpath(longDir));
  const QString source = longDir + QStringLiteral("/source.txt");
  const QString target = longDir + '/' + QString(80, QChar('t')) + QStringLiteral(".txt");
  BOOST_REQUIRE(target.size() > 260);
  BOOST_REQUIRE(Tests::writeFile(source, "new"));
  BOOST_REQUIRE(Tests::writeFile(target, "old"));

  BOOST_REQUIRE(core::CoreUtils::overwritingRename(source, target));
  BOOST_CHECK(!QFile::exists(source));
  BOOST_CHECK(Tests::readFile(target) == "new");

  // Also when the target doesn't exist yet.
  BOOST_REQUIRE(Tests::writeFile(source, "newer"));
  const QString other = longDir + '/' + QString(80, QChar('o')) + QStringLiteral(".txt");
  BOOST_REQUIRE(core::CoreUtils::overwritingRename(source, other));
  BOOST_CHECK(Tests::readFile(other) == "newer");
}

// The swap in reset() handed the object to deleteLater() while the pointer still held it.
BOOST_AUTO_TEST_CASE(a_safe_deleting_pointer_reset_to_its_own_object_keeps_it) {
  auto* obj = new QObject;
  const QPointer<QObject> watch(obj);
  {
    SafeDeletingQObjectPtr<QObject> ptr(obj);
    ptr.reset(ptr.get());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    BOOST_CHECK(!watch.isNull());
    BOOST_CHECK(ptr.get() == obj);
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  BOOST_CHECK(watch.isNull());
}

BOOST_AUTO_TEST_CASE(test_dpi_from_text_handles_null_ok_pointer) {
  BOOST_CHECK_EQUAL(core::CoreUtils::dpiFromText(QStringLiteral("abc"), nullptr), 0);
  BOOST_CHECK_EQUAL(core::CoreUtils::dpiFromText(QStringLiteral("300 dpi"), nullptr), 300);

  bool ok = true;
  BOOST_CHECK_EQUAL(core::CoreUtils::dpiFromText(QStringLiteral("no digits"), &ok), 0);
  BOOST_CHECK(!ok);

  BOOST_CHECK_EQUAL(core::CoreUtils::dpiFromText(QStringLiteral("600 dpi"), &ok), 600);
  BOOST_CHECK(ok);
}

#ifdef Q_OS_WIN
BOOST_AUTO_TEST_CASE(test_overwriting_rename_retries_transient_sharing_violation) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString source = dir.filePath("source.txt");
  const QString target = dir.filePath("target.txt");
  BOOST_REQUIRE(Tests::writeFile(source, "new"));
  BOOST_REQUIRE(Tests::writeFile(target, "old"));

  const std::wstring targetWide = QDir::toNativeSeparators(target).toStdWString();
  HANDLE lock = CreateFileW(targetWide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  BOOST_REQUIRE(lock != INVALID_HANDLE_VALUE);

  std::thread releaser([lock]() {
    Sleep(25);
    CloseHandle(lock);
  });

  const bool renamed = core::CoreUtils::overwritingRename(source, target);
  releaser.join();
  BOOST_CHECK(renamed);
  BOOST_CHECK(Tests::readFile(target) == "new");
}
#endif

BOOST_AUTO_TEST_SUITE_END()
