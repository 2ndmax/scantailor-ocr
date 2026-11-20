// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CoreUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include "ApplicationSettings.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <stdio.h>
#endif

namespace core {
#ifdef Q_OS_WIN
namespace {
/**
 * The path in the form that lets the Windows API handle more than MAX_PATH (260) characters,
 * whether or not long paths are enabled in Windows: absolute, with backslashes and "\\?\".
 */
std::wstring longWindowsPath(const QString& path) {
  QString native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
  if (native.startsWith(QLatin1String("\\\\?\\"))) {
    return native.toStdWString();
  }
  if (native.startsWith(QLatin1String("\\\\"))) {
    // A network path: \\server\share\... becomes \\?\UNC\server\share\...
    native = QLatin1String("\\\\?\\UNC\\") + native.mid(2);
  } else {
    native = QLatin1String("\\\\?\\") + native;
  }
  return native.toStdWString();
}
}  // namespace
#endif

bool CoreUtils::overwritingRename(const QString& from, const QString& to) {
#ifdef Q_OS_WIN
  // REVIEW-NOTE (2026-10): on Windows MoveFileExW fails while another handle has the target open
  // -> the thumbnail stays stale or saving reports an error. Not fixed because a retry loop adds
  // timing logic for a rare, visible case.
  // Write-through: the rename is on the disk when this returns, e.g. before a power failure.
  return MoveFileExW(longWindowsPath(from).c_str(), longWindowsPath(to).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
         != 0;
#else
  return rename(QFile::encodeName(from).data(), QFile::encodeName(to).data()) == 0;
#endif
}

void CoreUtils::maybeCreateCacheDir(const QString& outputDir) {
  QDir(outputDir).mkdir(QString::fromLatin1("cache"));

  // QDir::mkdir() returns false if the directory already exists,
  // so to prevent confusion this function return void.
}

QString CoreUtils::outputDirToThumbDir(const QString& outputDir) {
  return outputDir + QLatin1String("/cache/thumbs");
}

std::shared_ptr<ThumbnailPixmapCache> CoreUtils::createThumbnailCache(const QString& outputDir) {
  const QSize maxPixmapSize = ApplicationSettings::getInstance().getThumbnailQuality();
  const QString thumbsCachePath(outputDirToThumbDir(outputDir));
  return std::make_shared<ThumbnailPixmapCache>(thumbsCachePath, maxPixmapSize, 40, 5);
}

QString CoreUtils::qssConvertPxToEm(const QString& stylesheet, const double base, const int precision) {
  QString result = "";
  const QRegularExpression pxToEm(R"((\d+(\.\d+)?)px)");

  QRegularExpressionMatchIterator iter = pxToEm.globalMatch(stylesheet);
  int prevIndex = 0;
  while (iter.hasNext()) {
    QRegularExpressionMatch match = iter.next();
    result.append(stylesheet.mid(prevIndex, match.capturedStart() - prevIndex));

    double value = match.captured(1).toDouble();
    value /= base;
    result.append(QString::number(value, 'f', precision)).append("em");

    prevIndex = match.capturedEnd();
  }
  result.append(stylesheet.mid(prevIndex));
  return result;
}

QString CoreUtils::dpiText(const int dpi) {
  return QStringLiteral("%1 dpi").arg(dpi);
}

int CoreUtils::dpiFromText(const QString& text, bool* ok) {
  const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\d+")).match(text);
  if (!match.hasMatch()) {
    *ok = false;
    return 0;
  }
  return match.captured().toInt(ok);
}

}  // namespace core
