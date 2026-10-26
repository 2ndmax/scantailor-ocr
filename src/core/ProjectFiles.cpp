// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectFiles.h"

#include <QDir>
#include <QSet>

#include "PageSequence.h"
#include "ProjectPages.h"

namespace project_files {
namespace {
const QString projectExtension = QStringLiteral(".ScanTailor");
}  // namespace

QString fileNamePattern() {
  return '*' + projectExtension;
}

QString withProjectExtension(const QString& path) {
  if (path.isEmpty() || path.endsWith(projectExtension, Qt::CaseInsensitive)) {
    return path;
  }
  return path + projectExtension;
}

QString defaultProjectFile(const QString& inputDir) {
  QString name(QDir(inputDir).dirName());
  if (name.isEmpty()) {
    name = QStringLiteral("project");
  }
  return QDir::cleanPath(QDir(inputDir).filePath(name + projectExtension));
}

QString defaultOutputDir(const QString& inputDir) {
  return QDir::cleanPath(QDir(inputDir).filePath("out"));
}

bool isImportableImage(const QFileInfo& file) {
  static const QSet<QString> extensions{"png", "jpg", "jpeg", "tif", "tiff",
                                        // JPEG 2000: JP2 files, JPX / JPH files and raw codestreams.
                                        "jp2", "j2k", "j2c", "jpc", "jpf", "jpx", "jph", "jhc"};
  return extensions.contains(file.suffix().toLower());
}

QFileInfoList importableImagesIn(const QString& dir) {
  QFileInfoList images;
  for (const QFileInfo& file : QDir(dir).entryInfoList(QDir::Files)) {
    if (isImportableImage(file)) {
      images.push_back(file);
    }
  }
  return images;
}

bool allImagesExist(const ProjectPages& pages) {
  QSet<QString> checked;
  for (const PageInfo& page : pages.toPageSequence(IMAGE_VIEW)) {
    const QString path = page.imageId().filePath();
    if (checked.contains(path)) {
      continue;
    }
    checked.insert(path);
    if (!QFileInfo::exists(path)) {
      return false;
    }
  }
  return true;
}

bool isProjectInPlace(const ProjectPages& pages, const QString& outDir, const QString& inputDir) {
  // The folder the output folder belongs into must exist ...
  if (!QFileInfo(outDir).absoluteDir().exists()) {
    return false;
  }
  // ... and so must the images of the project. Otherwise the project has probably been
  // moved or copied to another computer, and the user should relink it.
  if (!allImagesExist(pages)) {
    return false;
  }
  if (pages.toPageSequence(IMAGE_VIEW).numPages() == 0) {
    return !inputDir.isEmpty() && QDir(inputDir).exists();
  }
  return true;
}

QString backupFilePath(const QString& projectFile) {
  const QFileInfo info(projectFile);
  const QString suffix = info.suffix().isEmpty() ? QString() : '.' + info.suffix();
  const QString base = info.dir().filePath(info.completeBaseName() + QLatin1String("_BACKUP"));
  QString path = base + suffix;
  for (int number = 2; QFileInfo::exists(path); ++number) {
    path = base + '_' + QString::number(number) + suffix;
  }
  return path;
}
}  // namespace project_files
