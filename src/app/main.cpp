// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <config.h>
#include <core/Application.h>
#include <core/ApplicationSettings.h>
#include <core/ColorSchemeFactory.h>
#include <core/ColorSchemeManager.h>
#include <core/FontIconPack.h>
#include <core/IconProvider.h>
#include <core/StyledIconPack.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

#include "MainWindow.h"

namespace {
// Before ScanTailor OCR 1.0.0 the program was called "scantailor-advanced".  Its settings,
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

int main(int argc, char* argv[]) {
  Application app(argc, argv);

#ifdef _WIN32
  // Get rid of all references to Qt's installation directory.
  Application::setLibraryPaths(QStringList(Application::applicationDirPath()));
#endif

  QStringList args = Application::arguments();

  // This information is used by QSettings.
  Application::setApplicationName(APPLICATION_NAME);
  Application::setOrganizationName(ORGANIZATION_NAME);

  QSettings::setDefaultFormat(QSettings::IniFormat);
  if (app.isPortableVersion()) {
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app.getPortableConfigPath());
  }
  takeOverOldSettings();
  QSettings settings;

  app.installLanguage(ApplicationSettings::getInstance().getLanguage());

  {
    std::unique_ptr<ColorScheme> scheme
        = ColorSchemeFactory().create(ApplicationSettings::getInstance().getColorScheme());
    ColorSchemeManager::instance().setColorScheme(*scheme);
  }
  IconProvider::getInstance().setIconPack(StyledIconPack::createDefault());

  auto* mainWnd = new MainWindow();
  mainWnd->setAttribute(Qt::WA_DeleteOnClose);
  if (settings.value("mainWindow/maximized") == false) {
    mainWnd->show();
  } else {
    // mainWnd->showMaximized();  // Doesn't work for Windows.
    QTimer::singleShot(0, mainWnd, &QMainWindow::showMaximized);
  }

  if (args.size() > 1) {
    mainWnd->openProject(args.at(1));
  }
  return Application::exec();
}  // main
