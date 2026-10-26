// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SettingsSetup.h"

#include <config.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

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
}  // namespace settings_setup
