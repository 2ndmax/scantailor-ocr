// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Application.h"

#include <config.h>

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFontDatabase>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <exception>

#include "OutOfMemoryHandler.h"

Application::Application(int& argc, char** argv) : QApplication(argc, argv), m_currentLocale("en") {
  initTranslations();
  initPortableVersion();
  loadFonts();
}

bool Application::notify(QObject* receiver, QEvent* e) {
  try {
    return QApplication::notify(receiver, e);
  } catch (const std::bad_alloc&) {
    OutOfMemoryHandler::instance().handleOutOfMemorySituation();
    return false;
  } catch (const std::exception& ex) {
    // An exception escaping an event handler would otherwise terminate the program silently.
    qCritical().noquote() << "Unexpected error while handling an event:" << ex.what();
    showUnexpectedError(QString::fromUtf8(ex.what()));
    return false;
  }
}

void Application::showUnexpectedError(const QString& reason) {
  // Shown with a delay, not from within the failing event handler, and only once
  // for a burst of errors.  notify() also runs in worker threads that have an event
  // loop, hence the atomic flag and the timer, which runs the lambda in the GUI thread.
  static std::atomic<bool> messagePending(false);
  if (messagePending.exchange(true)) {
    return;
  }
  QTimer::singleShot(0, this, [reason]() {
    QMessageBox::critical(activeWindow(), tr("Unexpected error"),
                          tr("An unexpected error occurred:\n%1\n\n"
                             "ScanTailor continues to run, but to be safe, save your project under "
                             "a new name and restart the program.")
                              .arg(reason));
    messagePending = false;
  });
}

void Application::installLanguage(const QString& requestedLocale) {
  // The default is the system's locale, e.g. "de_DE", while the translation may be just "de".
  QString locale = requestedLocale;
  if ((m_translationsMap.find(locale) == m_translationsMap.end()) && locale.contains('_')) {
    locale = locale.left(locale.indexOf('_'));
  }
  if (m_currentLocale == locale) {
    return;
  }

  if (m_translationsMap.find(locale) != m_translationsMap.end()) {
    bool loaded = m_translator.load(m_translationsMap[locale]);

    QCoreApplication::removeTranslator(&m_translator);
    QCoreApplication::installTranslator(&m_translator);

    m_currentLocale = (loaded) ? locale : "en";
  } else {
    QCoreApplication::removeTranslator(&m_translator);

    m_currentLocale = "en";
  }
}

const QString& Application::getCurrentLocale() const {
  return m_currentLocale;
}

std::list<QString> Application::getLanguagesList() const {
  std::list<QString> list{"en"};
  std::transform(m_translationsMap.begin(), m_translationsMap.end(), std::back_inserter(list),
                 [](const std::pair<QString, QString>& val) { return val.first; });
  return list;
}

void Application::initTranslations() {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
  auto opt = QString::SkipEmptyParts;
#else
  auto opt = Qt::SkipEmptyParts;
#endif
  const QStringList translationDirs(QString::fromUtf8(TRANSLATION_DIRS).split(QChar(':'), opt));

  // The files are named after the program, e.g. "scantailor-ocr_de.qm".
  const QStringList languageFileFilter(QString::fromUtf8(APPLICATION_NAME) + "_*.qm");
  for (const QString& path : translationDirs) {
    QDir dir = (QDir::isAbsolutePath(path)) ? QDir(path) : QDir::cleanPath(applicationDirPath() + '/' + path);
    if (dir.exists()) {
      QStringList translationFileNames = QDir(dir.path()).entryList(languageFileFilter);
      for (const QString& fileName : translationFileNames) {
        QString locale(fileName);
        locale.truncate(locale.lastIndexOf('.'));
        locale.remove(0, locale.indexOf('_') + 1);

        m_translationsMap[locale] = dir.absoluteFilePath(fileName);
      }
    }
  }
}

void Application::initPortableVersion() {
  const QString portableConfigDirName = QString::fromUtf8(PORTABLE_CONFIG_DIR);
  if (portableConfigDirName.isEmpty()) {
    return;
  }

  const QDir portableConfigPath(applicationDirPath() + '/' + portableConfigDirName);
  if ((portableConfigPath.exists() && QTemporaryDir(portableConfigPath.absolutePath()).isValid())
      || (!portableConfigPath.exists() && portableConfigPath.mkpath("."))) {
    m_portableConfigPath = portableConfigPath.absolutePath();
  }
}

void Application::loadFonts() {
  QDirIterator it(":/fonts");
  while (it.hasNext()) {
    QFontDatabase::addApplicationFont(it.next());
  }
}

bool Application::isPortableVersion() const {
  return !m_portableConfigPath.isNull();
}

const QString& Application::getPortableConfigPath() const {
  return m_portableConfigPath;
}
