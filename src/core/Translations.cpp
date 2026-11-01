// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Translations.h"

#include <config.h>

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <algorithm>
#include <iterator>

Translations::Translations() : m_currentLocale("en") {
  // The files are named after the program, e.g. "scantailor-ocr_de.qm".
  const QStringList languageFileFilter(QString::fromUtf8(APPLICATION_NAME) + "_*.qm");
  for (const QString& path : translationDirs()) {
    const QDir dir(path);
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

void Translations::install(const QString& requestedLocale) {
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
    m_currentLocale = (loaded) ? locale : "en";
    // Qt's translation is installed first, so the program's own one takes precedence.
    installQtLanguage(m_currentLocale);
    QCoreApplication::installTranslator(&m_translator);
  } else {
    QCoreApplication::removeTranslator(&m_translator);

    m_currentLocale = "en";
    installQtLanguage(m_currentLocale);
  }
}

void Translations::installQtLanguage(const QString& locale) {
  QCoreApplication::removeTranslator(&m_qtTranslator);
  if (locale == "en") {
    return;
  }
  // Shipped next to the program on Windows, part of the system's Qt on Linux.
  QStringList dirs = translationDirs();
  dirs.append(QLibraryInfo::path(QLibraryInfo::TranslationsPath));
  // QLocale also tries the country variants, e.g. "qtbase_pt_BR" for "pt".
  const QLocale qtLocale(locale);
  for (const QString& dir : dirs) {
    if (m_qtTranslator.load(qtLocale, "qtbase", "_", dir) || m_qtTranslator.load(qtLocale, "qt", "_", dir)) {
      QCoreApplication::installTranslator(&m_qtTranslator);
      return;
    }
  }
}

std::list<QString> Translations::languages() const {
  std::list<QString> list{"en"};
  std::transform(m_translationsMap.begin(), m_translationsMap.end(), std::back_inserter(list),
                 [](const std::pair<QString, QString>& val) { return val.first; });
  return list;
}

QStringList Translations::translationDirs() {
  auto opt = Qt::SkipEmptyParts;
  QStringList dirs;
  for (const QString& path : QString::fromUtf8(TRANSLATION_DIRS).split(QChar(':'), opt)) {
    dirs.append(QDir::isAbsolutePath(path) ? QDir::cleanPath(path)
                                           : QDir::cleanPath(QCoreApplication::applicationDirPath() + '/' + path));
  }
  return dirs;
}
