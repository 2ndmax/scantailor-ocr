// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <core/Application.h>
#include <core/ApplicationSettings.h>
#include <core/ColorSchemeFactory.h>
#include <core/ColorSchemeManager.h>
#include <core/IconProvider.h>
#include <core/ImageSizeLimit.h>
#include <core/OcrEngine.h>
#include <core/SettingsSetup.h>
#include <core/StyledIconPack.h>

#include <QGuiApplication>
#include <QPalette>
#include <QSettings>
#include <QStringList>
#include <QStyleHints>
#include <QTimer>

#include "MainWindow.h"

int main(int argc, char* argv[]) {
  OcrEngine::limitThreads();
  Application app(argc, argv);

  ImageSizeLimit::applyQtAllocationLimit();

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
    // The title bars are drawn by the system, which colors them after the system's light or dark mode,
    // not after the app. The Light and Dark schemes tell it their own; System keeps the system's.
    // Set before the scheme, so that a palette Qt derives from the new mode can't replace the scheme's.
    // Qt offers this only from 6.8 on; with an older Qt the title bars stay with the system's mode.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    if (const QPalette* palette = scheme->getPalette()) {
      const bool darkScheme = palette->color(QPalette::Window).lightness() < 128;
      QGuiApplication::styleHints()->setColorScheme(darkScheme ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
    }
#endif
    ColorSchemeManager::instance().setColorScheme(*scheme);
  }
  IconProvider::getInstance().setIconPack(StyledIconPack::createDefault());

  auto* mainWnd = new MainWindow();
  mainWnd->setAttribute(Qt::WA_DeleteOnClose);
  // Without the setting (first start), the window opens maximized on purpose.
  if (settings.value("mainWindow/maximized") == false) {
    mainWnd->show();
  } else {
    // mainWnd->showMaximized();  // Doesn't work for Windows.
    QTimer::singleShot(0, mainWnd, &QMainWindow::showMaximized);
  }

  // The project file and the step restartProgram() reopens it to, as "--select-step=<n>" (0-based).
  QString projectFile;
  int selectStep = -1;
  for (int i = 1; i < args.size(); ++i) {
    const QString& arg = args.at(i);
    if (arg.startsWith(QStringLiteral("--select-step="))) {
      bool ok = false;
      const int step = arg.mid(14).toInt(&ok);
      if (ok) {
        selectStep = step;
      }
    } else if (projectFile.isEmpty()) {
      projectFile = arg;
    }
  }
  if (!projectFile.isEmpty()) {
    mainWnd->openProject(projectFile, selectStep);
  }
  return Application::exec();
}  // main
