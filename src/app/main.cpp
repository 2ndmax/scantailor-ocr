// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <core/Application.h>
#include <core/ApplicationSettings.h>
#include <core/ColorSchemeFactory.h>
#include <core/ColorSchemeManager.h>
#include <core/IconProvider.h>
#include <core/ImageSizeLimit.h>
#include <core/SettingsSetup.h>
#include <core/StyledIconPack.h>

#include <QGuiApplication>
#include <QImageReader>
#include <QSettings>
#include <QStringList>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
  Application app(argc, argv);

  // Qt 6 refuses to decode images needing more than 256 MB by default, which large color scans
  // (e.g. A3 at 600 dpi) easily exceed. The limit in MB fits the largest image allowed, decoded with
  // 16 bit per channel (8 bytes per pixel). It only matters for formats that don't tell their size
  // before decoding, as ImageLoader checks the size from the header first.
  QImageReader::setAllocationLimit(static_cast<int>(ImageSizeLimit::MAX_PIXELS * 8 / (1024 * 1024)) + 1);

#ifdef _WIN32
  // Get rid of all references to Qt's installation directory.
  Application::setLibraryPaths(QStringList(Application::applicationDirPath()));
#endif

  QStringList args = Application::arguments();

  settings_setup::setUp();
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
