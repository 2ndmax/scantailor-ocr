// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <SettingsSetup.h>
#include <config.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(SettingsSetupTestSuite)

BOOST_AUTO_TEST_CASE(test_portable_config_folder_is_next_to_the_program) {
  const QTemporaryDir programDir;
  BOOST_REQUIRE(programDir.isValid());
  const QString portableConfigDirName = QString::fromUtf8(PORTABLE_CONFIG_DIR);

  const QString path = settings_setup::portableConfigPath(programDir.path());

  if (portableConfigDirName.isEmpty()) {
    BOOST_CHECK(path.isNull());
    return;
  }
  BOOST_CHECK(path == QDir(programDir.path()).absoluteFilePath(portableConfigDirName));
  BOOST_CHECK(QDir(path).exists());
  // A folder that already exists is used as well, and the writability check leaves nothing behind.
  BOOST_CHECK(settings_setup::portableConfigPath(programDir.path()) == path);
  BOOST_CHECK(QDir(path).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_no_portable_config_if_the_folder_cannot_be_made) {
  const QTemporaryDir programDir;
  BOOST_REQUIRE(programDir.isValid());
  const QString portableConfigDirName = QString::fromUtf8(PORTABLE_CONFIG_DIR);
  if (!portableConfigDirName.isEmpty()) {
    // A file with the folder's name blocks it.
    QFile blocker(QDir(programDir.path()).absoluteFilePath(portableConfigDirName));
    BOOST_REQUIRE(blocker.open(QIODevice::WriteOnly));
  }

  BOOST_CHECK(settings_setup::portableConfigPath(programDir.path()).isNull());
}

BOOST_AUTO_TEST_SUITE_END()
