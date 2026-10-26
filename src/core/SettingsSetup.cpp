// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SettingsSetup.h"

#include <config.h>

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "ApplicationSettings.h"
#include "ThreadBudget.h"

namespace {
// Before ScanTailor OCR 1.0.0 the program was called "scantailor-advanced". Its settings,
// profiles and OCR languages are taken over once, so they aren't lost with the new name.
// The old files are copied, not moved; they stay usable for ScanTailor Advanced.
const QString oldApplicationName = QStringLiteral("scantailor-advanced");

void copyDirectory(const QString& from, const QString& to) {
  const QDir fromDir(from);
  QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file = it.next();
    const QString target = to + '/' + fromDir.relativeFilePath(file);
    QDir().mkpath(QFileInfo(target).absolutePath());
    QFile::copy(file, target);
  }
}

void takeOverOldSettings() {
  QSettings settings;
  if (!settings.allKeys().isEmpty()) {
    return;
  }

  const QSettings oldSettings(QSettings::IniFormat, QSettings::UserScope, oldApplicationName, oldApplicationName);
  for (const QString& key : oldSettings.allKeys()) {
    settings.setValue(key, oldSettings.value(key));
  }
  settings.sync();

  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  const QString applicationName = QCoreApplication::applicationName();
  const QString organizationName = QCoreApplication::organizationName();
  QCoreApplication::setApplicationName(oldApplicationName);
  QCoreApplication::setOrganizationName(oldApplicationName);
  const QString oldAppData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  QCoreApplication::setApplicationName(applicationName);
  QCoreApplication::setOrganizationName(organizationName);
  if (!appData.isEmpty() && !QFileInfo::exists(appData) && QFileInfo(oldAppData).isDir()) {
    copyDirectory(oldAppData, appData);
  }
}
}  // namespace

namespace settings_setup {
QString portableConfigPath(const QString& programDir) {
  const QString portableConfigDirName = QString::fromUtf8(PORTABLE_CONFIG_DIR);
  if (portableConfigDirName.isEmpty()) {
    return QString();
  }

  const QDir portableConfigPath(programDir + '/' + portableConfigDirName);
  // A temporary directory *inside* the config directory proves it is writable (the path alone
  // would only be a template for a sibling in the program directory).
  if ((portableConfigPath.exists() && QTemporaryDir(portableConfigPath.absoluteFilePath("XXXXXX")).isValid())
      || (!portableConfigPath.exists() && portableConfigPath.mkpath("."))) {
    return portableConfigPath.absolutePath();
  }
  return QString();
}

QString portableConfigPath() {
  static const QString path = portableConfigPath(QCoreApplication::applicationDirPath());
  return path;
}

void setUp() {
  // This information is used by QSettings.
  QCoreApplication::setApplicationName(APPLICATION_NAME);
  QCoreApplication::setOrganizationName(ORGANIZATION_NAME);

  QSettings::setDefaultFormat(QSettings::IniFormat);
  const QString portablePath = portableConfigPath();
  if (!portablePath.isNull()) {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, portablePath);
  }
  takeOverOldSettings();
  ThreadBudget::setMaxThreads(ApplicationSettings::getInstance().getProcessingThreads());
}
}  // namespace settings_setup
