// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AutoImportUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <map>

#include "ImageFileInfo.h"
#include "PageInfo.h"
#include "PageSequence.h"
#include "ProjectPages.h"

namespace auto_import_utils {
QString imagePathKey(const QString& filePath) {
  const QString path(QDir::cleanPath(QFileInfo(filePath).absoluteFilePath()));
#ifdef Q_OS_WIN
  return path.toCaseFolded();
#else
  return path;
#endif
}

QSet<QString> projectImagePathKeys(const ProjectPages& pages) {
  QSet<QString> keys;
  for (const PageInfo& page : pages.toPageSequence(IMAGE_VIEW)) {
    if (!page.imageId().filePath().isEmpty()) {
      keys.insert(imagePathKey(page.imageId().filePath()));
    }
  }
  return keys;
}

int countImagesWithoutDpi(const ProjectPages& pages) {
  int count = 0;
  for (const ImageFileInfo& info : pages.toImageFileInfo()) {
    if (!info.isDpiOK()) {
      ++count;
    }
  }
  return count;
}

QString suggestedImportDirectory(const ProjectPages& pages, const QString& emptyProjectInputDir) {
  if (pages.numImages() == 0) {
    return emptyProjectInputDir;
  }
  std::map<QString, int> counts;
  QString best;
  for (const PageInfo& page : pages.toPageSequence(IMAGE_VIEW)) {
    const QString dir = QFileInfo(page.imageId().filePath()).absolutePath();
    const int count = ++counts[dir];
    if (best.isEmpty() || (count > counts[best])) {
      best = dir;
    }
  }
  return best;
}

bool renameRetrying(const QString& from, const QString& to) {
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (QFile::rename(from, to)) {
      return true;
    }
    QThread::msleep(100);
  }
  return false;
}
}  // namespace auto_import_utils
