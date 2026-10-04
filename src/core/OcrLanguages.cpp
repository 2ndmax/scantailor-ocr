// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OcrLanguages.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace {
const QString kSuffix = QStringLiteral(".traineddata");
const QString kScriptPrefix = QStringLiteral("script/");

/** Adds the language files directly in \p dirPath, with \p codePrefix in front of their names. */
void collectLanguages(const QString& dirPath,
                      const QString& subDir,
                      const QString& codePrefix,
                      std::map<QString, QString>* languages) {
  const QDir dir(subDir.isEmpty() ? dirPath : QDir(dirPath).filePath(subDir));
  if (!dir.exists()) {
    return;
  }
  const QFileInfoList files = dir.entryInfoList(QStringList("*" + kSuffix), QDir::Files | QDir::Readable);
  for (const QFileInfo& file : files) {
    const QString name = file.fileName().chopped(kSuffix.size());
    if (name.isEmpty() || !OcrLanguages::isRecognitionModel(codePrefix + name)) {
      continue;
    }
    // emplace() keeps an existing entry, so the first folder wins.
    languages->emplace(codePrefix + name, dirPath);
  }
}

/** Whether files can be created in the folder.  Creates it if necessary. */
bool isWritableDir(const QString& dirPath) {
  if (!QDir().mkpath(dirPath)) {
    return false;
  }
  // QFileInfo::isWritable() isn't reliable on Windows, so actually try it.
  QTemporaryFile test(QDir(dirPath).filePath("write-test-XXXXXX"));
  return test.open();
}
}  // namespace

QStringList OcrLanguages::searchDirs() {
  QStringList dirs;
  auto add = [&dirs](const QString& dir) {
    if (dir.isEmpty()) {
      return;
    }
    const QString clean = QDir::cleanPath(QDir(dir).absolutePath());
    if (!dirs.contains(clean)) {
      dirs.push_back(clean);
    }
  };

  add(programDir());
  add(userDir());
  // Since Tesseract 4, TESSDATA_PREFIX is the folder with the language files itself.
  add(QString::fromLocal8Bit(qgetenv("TESSDATA_PREFIX")));
#ifndef Q_OS_WIN
  add("/usr/share/tesseract-ocr/5/tessdata");
  add("/usr/share/tesseract-ocr/4.00/tessdata");
  add("/usr/share/tessdata");
  add("/usr/local/share/tessdata");
#endif
  return dirs;
}

QString OcrLanguages::programDir() {
  return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("tessdata"));
}

QString OcrLanguages::userDir() {
  const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (appData.isEmpty()) {
    return QString();
  }
  return QDir::cleanPath(QDir(appData).filePath("tessdata"));
}

std::map<QString, QString> OcrLanguages::available() {
  std::map<QString, QString> languages;
  for (const QString& dirPath : searchDirs()) {
    collectLanguages(dirPath, QString(), QString(), &languages);
    collectLanguages(dirPath, "script", kScriptPrefix, &languages);
  }
  return languages;
}

QString OcrLanguages::filePath(const QString& dir, const QString& code) {
  return QDir(dir).filePath(code + kSuffix);
}

QString OcrLanguages::commonDir(const QStringList& codes) {
  if (codes.isEmpty()) {
    return QString();
  }
  for (const QString& dirPath : searchDirs()) {
    bool all = true;
    for (const QString& code : codes) {
      if (!QFileInfo(filePath(dirPath, code)).isFile()) {
        all = false;
        break;
      }
    }
    if (all) {
      return dirPath;
    }
  }
  return QString();
}

QString OcrLanguages::gatherInUserDir(const QStringList& codes, QString* error) {
  const QString target = userDir();
  if (target.isEmpty() || !isWritableDir(target)) {
    if (error) {
      *error = QCoreApplication::translate("OcrLanguages", "The folder %1 can't be written to.")
                   .arg(QDir::toNativeSeparators(target));
    }
    return QString();
  }

  const std::map<QString, QString> languages = available();
  for (const QString& code : codes) {
    const QString targetFile = filePath(target, code);
    if (QFileInfo(targetFile).isFile()) {
      continue;
    }
    const auto it = languages.find(code);
    if (it == languages.end()) {
      if (error) {
        *error = QCoreApplication::translate("OcrLanguages", "The language file of \"%1\" wasn't found.").arg(code);
      }
      return QString();
    }
    // Copy to a temporary name first, so an interrupted copy doesn't leave a damaged file.
    const QString partFile = targetFile + ".part";
    QFile::remove(partFile);
    const bool ok = QDir().mkpath(QFileInfo(targetFile).absolutePath())
                    && QFile::copy(filePath(it->second, code), partFile) && QFile::rename(partFile, targetFile);
    if (!ok) {
      QFile::remove(partFile);
      if (error) {
        *error = QCoreApplication::translate("OcrLanguages", "Could not copy the language file of \"%1\" to %2.")
                     .arg(code, QDir::toNativeSeparators(target));
      }
      return QString();
    }
  }
  return target;
}

QString OcrLanguages::downloadDir(QString* error) {
  for (const QString& dir : {programDir(), userDir()}) {
    if (!dir.isEmpty() && isWritableDir(dir)) {
      return dir;
    }
  }
  if (error) {
    *error = QCoreApplication::translate("OcrLanguages", "Neither %1 nor %2 can be written to.")
                 .arg(QDir::toNativeSeparators(programDir()), QDir::toNativeSeparators(userDir()));
  }
  return QString();
}

bool OcrLanguages::isScript(const QString& code) {
  return code.startsWith(kScriptPrefix);
}

bool OcrLanguages::isRecognitionModel(const QString& code) {
  // "osd" detects the orientation of pages, "equ" finds equations for the legacy engine.
  return (code != "osd") && (code != "equ");
}

QString OcrLanguages::displayName(const QString& code) {
  if (isScript(code)) {
    // Scripts are named in English by Tesseract, e.g. "script/Latin".
    return QCoreApplication::translate("OcrLanguages", "%1 (script)").arg(code.mid(kScriptPrefix.size()));
  }

  // Tesseract names languages by ISO 639-2 codes, optionally followed by a script or variant.
  const int underscore = code.indexOf('_');
  const QString base = (underscore < 0) ? code : code.left(underscore);
  const QString variant = (underscore < 0) ? QString() : code.mid(underscore + 1);

  // Historical languages and others Qt doesn't know.
  static const std::map<QString, const char*> extraNames = {
      {"enm", QT_TRANSLATE_NOOP("OcrLanguages", "Middle English")},
      {"frm", QT_TRANSLATE_NOOP("OcrLanguages", "Middle French")},
      {"grc", QT_TRANSLATE_NOOP("OcrLanguages", "Ancient Greek")},
      {"kmr", QT_TRANSLATE_NOOP("OcrLanguages", "Kurmanji")},
      {"frk", QT_TRANSLATE_NOOP("OcrLanguages", "German Fraktur (old model)")},
      // Qt only knows Bokmål ("nob") and Nynorsk ("nno").  Like the other living
      // languages, it's shown by its own name, so it isn't marked for translation.
      {"nor", "Norsk"},
  };
  const auto extra = extraNames.find(base);

  QString name;
  if (extra != extraNames.end()) {
    name = QCoreApplication::translate("OcrLanguages", extra->second);
  }
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
  if (name.isEmpty()) {
    const QLocale::Language language = QLocale::codeToLanguage(base, QLocale::ISO639Part2);
    if ((language != QLocale::AnyLanguage) && (language != QLocale::C)) {
      name = QLocale(language).nativeLanguageName();
      if (name.isEmpty()) {
        name = QLocale::languageToString(language);
      }
    }
  }
#endif
  if (name.isEmpty()) {
    return code;
  }
  name[0] = name[0].toUpper();
  if (!variant.isEmpty()) {
    name += ", " + variant;
  }
  return QString("%1 (%2)").arg(name, code);
}
