// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PathNames.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace path_names {
namespace {
/**
 * Windows reserves these names whatever the letter case, also with an extension or spaces in
 * front of it, as in "nul.txt" or "nul .txt". The numbers include the superscript digits 1 to 3.
 */
bool isReservedOnWindows(const QString& name) {
  static const QRegularExpression reserved(
      QStringLiteral("^(CON|PRN|AUX|NUL|(COM|LPT)[0-9\\x{00B9}\\x{00B2}\\x{00B3}]) *(\\.|$)"),
      QRegularExpression::CaseInsensitiveOption);
  return reserved.match(name).hasMatch();
}

/**
 * Asks for the exact name instead of using QFileInfo::exists(). Windows ignores a dot or space
 * at the end of a name and knows reserved names everywhere, so it would call "D:/Books." or
 * "D:/NUL" existing although no such entry is in the folder.
 */
bool isEntryOfFolder(const QString& name, const QString& folder) {
#ifdef Q_OS_WIN
  const Qt::CaseSensitivity caseSensitivity = Qt::CaseInsensitive;
#else
  const Qt::CaseSensitivity caseSensitivity = Qt::CaseSensitive;
#endif
  return QDir(folder)
      .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)
      .contains(name, caseSensitivity);
}
}  // namespace

QString invalidNameReason(const QString& name) {
  if (name.isEmpty()) {
    return QCoreApplication::translate("PathNames", "The name is empty.");
  }

  static const QString forbiddenCharacters = QStringLiteral("\\/:*?\"<>|");
  for (const QChar character : name) {
    if (character.category() == QChar::Other_Control) {
      return QCoreApplication::translate("PathNames", "The name \"%1\" contains a control character.").arg(name);
    }
    if (forbiddenCharacters.contains(character)) {
      return QCoreApplication::translate("PathNames", "The name \"%1\" contains the character \"%2\".")
          .arg(name, QString(character));
    }
  }

  // Windows removes them silently, so the path in the project would no longer be right.
  if (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' '))) {
    return QCoreApplication::translate("PathNames", "The name \"%1\" ends with a period or a space.").arg(name);
  }

  if (isReservedOnWindows(name)) {
    return QCoreApplication::translate("PathNames", "The name \"%1\" is reserved on Windows.").arg(name);
  }
  return QString();
}

QString invalidNewPathReason(const QString& path) {
  if (path.isEmpty()) {
    return invalidNameReason(path);
  }

  // Resolves "." and "..", which are not names, and a separator at the end.
  QString current = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  while (true) {
    const QFileInfo part(current);
    const QString name = part.fileName();
    const QString parent = part.path();
    const bool reachedRoot = name.isEmpty() || (parent == current);
    if (reachedRoot) {
      return QString();
    }

    const QString reason = invalidNameReason(name);
    if (!reason.isEmpty() && !isEntryOfFolder(name, parent)) {
      return reason;
    }
    current = parent;
  }
}
}  // namespace path_names
