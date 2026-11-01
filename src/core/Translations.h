// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TRANSLATIONS_H_
#define SCANTAILOR_CORE_TRANSLATIONS_H_

#include <QString>
#include <QStringList>
#include <QTranslator>
#include <list>
#include <map>

#include "NonCopyable.h"

/**
 * The program's translations and Qt's own ones. Works with a QCoreApplication, so the
 * command line tool shows its messages in the language of the program, too.
 * Create it after the application object.
 */
class Translations {
  DECLARE_NON_COPYABLE(Translations)

 public:
  /** Finds the translations shipped with the program; none is installed yet. */
  Translations();

  /**
   * Installs the translation for \p requestedLocale, e.g. "de" or the system's "de_DE".
   * Without one, English, the source language, is used.
   */
  void install(const QString& requestedLocale);

  /** The installed language, e.g. "de"; "en" for English. */
  const QString& currentLocale() const { return m_currentLocale; }

  /** The languages there are translations for, and "en". */
  std::list<QString> languages() const;

 private:
  /** The folders searched for the program's translations, as configured in TRANSLATION_DIRS. */
  static QStringList translationDirs();

  /** Loads Qt's own translation (standard buttons, dialogs, context menus) for \p locale. */
  void installQtLanguage(const QString& locale);

  QTranslator m_translator;
  QTranslator m_qtTranslator;
  QString m_currentLocale;
  std::map<QString, QString> m_translationsMap;
};

#endif  // SCANTAILOR_CORE_TRANSLATIONS_H_
