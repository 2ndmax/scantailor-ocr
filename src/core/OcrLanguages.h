// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OCRLANGUAGES_H_
#define SCANTAILOR_CORE_OCRLANGUAGES_H_

#include <QString>
#include <QStringList>
#include <map>

/**
 * \brief Finds the language files (*.traineddata) of Tesseract.
 *
 * They are looked up in "tessdata" next to the program, in "tessdata" in the
 * user's application data folder, in the folder TESSDATA_PREFIX points to and,
 * except on Windows, in the usual system folders.  Script models are expected
 * in a "script" subfolder and have codes like "script/Latin", which is also how
 * Tesseract refers to them.
 */
class OcrLanguages {
 public:
  /** The folders searched, in order of preference, whether they exist or not. */
  static QStringList searchDirs();

  /** "tessdata" next to the program. */
  static QString programDir();

  /** "tessdata" in the user's application data folder. */
  static QString userDir();

  /**
   * \brief The available languages: code (e.g. "deu") to the folder holding its file.
   *
   * If a language exists in several folders, the first one in searchDirs() order wins.
   * "osd" (orientation and script detection) isn't a language and is left out.
   */
  static std::map<QString, QString> available();

  /** The language file of \p code in the folder \p dir. */
  static QString filePath(const QString& dir, const QString& code);

  /**
   * \brief A folder that contains the files of all given languages, or an empty string.
   *
   * Tesseract loads all languages of one recognition run from a single folder.
   */
  static QString commonDir(const QStringList& codes);

  /**
   * \brief Copies the files of the given languages that are missing there into userDir().
   *
   * For languages spread over several folders.  Returns userDir(), or an empty
   * string on failure, with the reason in \p error.
   */
  static QString gatherInUserDir(const QStringList& codes, QString* error);

  /**
   * \brief Where downloaded language files go.
   *
   * programDir() if files can be created there, otherwise userDir().
   * Returns an empty string if neither is writable.
   */
  static QString downloadDir(QString* error);

  /** Whether the code is that of a script model, like "script/Latin". */
  static bool isScript(const QString& code);

  /**
   * \brief False for files that aren't recognition models for a language or script.
   *
   * These are "osd" (page orientation) and "equ" (equations, legacy engine only).
   */
  static bool isRecognitionModel(const QString& code);

  /** A readable name, e.g. "Deutsch (deu)", "Deutsch, latf (deu_latf)" or "Latin (script)". */
  static QString displayName(const QString& code);
};


#endif  // SCANTAILOR_CORE_OCRLANGUAGES_H_
