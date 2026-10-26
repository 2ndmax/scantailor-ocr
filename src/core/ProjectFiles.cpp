// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectFiles.h"

#include <QDir>
#include <QSet>

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
}  // namespace project_files
