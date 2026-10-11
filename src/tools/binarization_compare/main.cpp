// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: binarizes sample pages with our methods and with those of ScanTailor
// Experimental, and writes a web page to compare the results side by side.
//
// Usage: binarization_compare <output folder> <image file or folder>...
// Pages already in the output folder are only computed again when the image file changed,
// so more sample pages can be added later.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <cmath>
#include <iostream>

#include "GrayImage.h"
#include "ImageLoader.h"
#include "Methods.h"
#include "Viewer.h"

using namespace binarization_compare;

namespace {
const QStringList kImageSuffixes = {"tif", "tiff", "png", "jpg", "jpeg", "jp2", "bmp"};

// Changes whenever the compared methods change, so that all pages are computed again.
QString methodsStamp() {
  QStringList parts;
  for (const Method& method : methods()) {
    parts << method.id << method.parameters;
  }
  return QString::fromLatin1(QCryptographicHash::hash(parts.join('|').toUtf8(), QCryptographicHash::Md5).toHex());
}

QString pageId(const QFileInfo& file) {
  QString id = file.completeBaseName();
  id.replace(QRegularExpression("[^A-Za-z0-9_.-]+"), "_");
  // Long names (copies of copies) are shortened, a hash of the full name keeps them apart.
  const QString hash = QString::fromLatin1(
      QCryptographicHash::hash(file.fileName().toUtf8(), QCryptographicHash::Md5).toHex().left(6));
  return id.left(40) + "_" + hash;
}

void collect(const QString& path, QList<QFileInfo>& files) {
  const QFileInfo info(path);
  if (info.isDir()) {
    for (const QFileInfo& entry : QDir(path).entryInfoList(QDir::Files, QDir::Name)) {
      if (kImageSuffixes.contains(entry.suffix().toLower())) {
        files << entry;
      }
    }
  } else if (info.isFile()) {
    files << info;
  } else {
    std::cerr << "Not found: " << qPrintable(path) << std::endl;
  }
}

bool processPage(const QFileInfo& file, const QDir& outDir) {
  const QString id = pageId(file);
  const QString stamp
      = QString("%1|%2|%3").arg(file.size()).arg(file.lastModified().toMSecsSinceEpoch()).arg(methodsStamp());
  QDir pageDir(outDir.filePath(id));
  {
    QFile infoFile(pageDir.filePath("info.json"));
    if (infoFile.open(QIODevice::ReadOnly)
        && QJsonDocument::fromJson(infoFile.readAll()).object().value("stamp").toString() == stamp) {
      std::cout << "unchanged: " << qPrintable(file.fileName()) << std::endl;
      return true;
    }
  }

  std::cout << "page: " << qPrintable(file.fileName()) << std::endl;
  const QImage image = ImageLoader::load(file.filePath());
  if (image.isNull()) {
    std::cerr << "  could not be loaded" << std::endl;
    return false;
  }
  outDir.mkpath(id);
  const imageproc::GrayImage gray(image);
  gray.toQImage().save(pageDir.filePath("original.jpg"), "JPG", 90);
  gray.toQImage().scaledToWidth(600, Qt::SmoothTransformation).save(pageDir.filePath("overview.jpg"), "JPG", 85);

  QJsonArray results;
  for (const Method& method : methods()) {
    QElapsedTimer timer;
    timer.start();
    const QImage result = method.run(gray);
    const qint64 ms = timer.elapsed();
    const QString fileName = method.id + ((method.group == "filter") ? ".jpg" : ".png");
    result.save(pageDir.filePath(fileName), (method.group == "filter") ? "JPG" : "PNG", 90);
    std::cout << "  " << qPrintable(method.label) << ": " << ms << " ms" << std::endl;
    results.append(QJsonObject{{"id", method.id},
                               {"label", method.label},
                               {"group", method.group},
                               {"parameters", method.parameters},
                               {"ms", ms},
                               {"file", fileName}});
  }

  const int dpi = static_cast<int>(std::lround(image.dotsPerMeterX() * 0.0254));
  // Only the file name, not the folder, which may contain personal names.
  const QJsonObject info{{"id", id},   {"file", file.fileName()}, {"width", gray.width()}, {"height", gray.height()},
                         {"dpi", dpi}, {"stamp", stamp},          {"results", results}};
  QFile infoFile(pageDir.filePath("info.json"));
  if (!infoFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return false;
  }
  infoFile.write(QJsonDocument(info).toJson());
  return true;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() < 3) {
    std::cerr << "Usage: binarization_compare <output folder> <image file or folder>..." << std::endl;
    return 2;
  }
  const QDir outDir(args[1]);
  if (!outDir.mkpath(".")) {
    std::cerr << "Cannot create " << qPrintable(args[1]) << std::endl;
    return 1;
  }

  QList<QFileInfo> files;
  for (int i = 2; i < args.size(); ++i) {
    collect(args[i], files);
  }
  int failed = 0;
  for (const QFileInfo& file : files) {
    if (!processPage(file, outDir)) {
      ++failed;
    }
  }
  if (!writeViewer(outDir)) {
    std::cerr << "Cannot write the viewer" << std::endl;
    return 1;
  }
  std::cout << "Done: " << qPrintable(QDir::toNativeSeparators(outDir.filePath("index.html"))) << std::endl;
  return (failed == 0) ? 0 : 1;
}
