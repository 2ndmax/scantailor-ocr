// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: compares despeckling results before and after a change of Despeckle.cpp.
//
// Usage:
//   despeckle_compare run <input folder> <output folder> [--dpi=200]
//     Binarizes each scan (Otsu) and despeckles it with the three levels; writes <name>_bin.png and
//     <name>_<level>.png. Run it once with the old build and once with the new one.
//   despeckle_compare diff <before folder> <after folder> <output folder>
//     For each result present in both folders, writes <name>_<level>_diff.png: black = kept by both,
//     light gray = removed by both, red = removed only after the change, green = kept only after it.
//     Also writes <name>_<level>_spots.png with enlarged crops around the changes and prints counts.

#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <Despeckle.h>
#include <Dpi.h>
#include <Grayscale.h>
#include <ImageLoader.h>
#include <NullTaskStatus.h>

#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <iostream>
#include <vector>

using namespace imageproc;

namespace {
struct LevelName {
  Despeckle::Level level;
  const char* name;
};

const LevelName kLevels[]
    = {{Despeckle::CAUTIOUS, "cautious"}, {Despeckle::NORMAL, "normal"}, {Despeckle::AGGRESSIVE, "aggressive"}};

int run(const QString& inDir, const QString& outDir, const int dpi) {
  QDir().mkpath(outDir);
  const QStringList files = QDir(inDir).entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    const QImage image = ImageLoader::load(QDir(inDir).filePath(file));
    if (image.isNull()) {
      std::cerr << "cannot read " << file.toStdString() << "\n";
      continue;
    }
    const QString base = QDir(outDir).filePath(QFileInfo(file).completeBaseName());
    const QImage gray = toGrayscale(image);
    const BinaryImage binary(gray, BinaryThreshold::otsuThreshold(gray));
    binary.toQImage().save(base + "_bin.png");
    for (const LevelName& level : kLevels) {
      const BinaryImage result = Despeckle::despeckle(binary, Dpi(dpi, dpi), level.level, NullTaskStatus());
      result.toQImage().save(base + "_" + level.name + ".png");
    }
    std::cout << file.toStdString() << " done" << std::endl;
  }
  return 0;
}

bool isBlack(const QImage& image, int x, int y) {
  return qGray(image.pixel(x, y)) < 128;
}

int diff(const QString& beforeDir, const QString& afterDir, const QString& outDir) {
  QDir().mkpath(outDir);
  const QStringList files = QDir(afterDir).entryList({"*.png"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    if (file.endsWith("_bin.png") || !QFileInfo::exists(QDir(beforeDir).filePath(file))) {
      continue;
    }
    const QString name = QFileInfo(file).completeBaseName();
    const QString binFile = name.left(name.lastIndexOf('_')) + "_bin.png";
    const QImage bin(QDir(afterDir).filePath(binFile));
    const QImage before(QDir(beforeDir).filePath(file));
    const QImage after(QDir(afterDir).filePath(file));
    if (bin.isNull() || (before.size() != after.size()) || (bin.size() != after.size())) {
      std::cerr << "skipping " << file.toStdString() << "\n";
      continue;
    }
    QImage out(after.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    int newlyRemoved = 0;
    int newlyKept = 0;
    std::vector<QPoint> changes;
    for (int y = 0; y < after.height(); ++y) {
      for (int x = 0; x < after.width(); ++x) {
        const bool b = isBlack(before, x, y);
        const bool a = isBlack(after, x, y);
        if (b && a) {
          out.setPixel(x, y, qRgb(0, 0, 0));
        } else if (b) {
          out.setPixel(x, y, qRgb(230, 0, 0));
          ++newlyRemoved;
          changes.emplace_back(x, y);
        } else if (a) {
          out.setPixel(x, y, qRgb(0, 170, 0));
          ++newlyKept;
          changes.emplace_back(x, y);
        } else if (isBlack(bin, x, y)) {
          out.setPixel(x, y, qRgb(200, 200, 200));
        }
      }
    }
    out.save(QDir(outDir).filePath(name + "_diff.png"));
    std::cout << name.toStdString() << ": newly removed " << newlyRemoved << " px, newly kept " << newlyKept << " px\n";

    // Enlarged crops around the changes, one per 60x60 cell, at most 48.
    const int cell = 60;
    const int scale = 4;
    std::vector<QPoint> spots;
    for (const QPoint& p : changes) {
      const QPoint key(p.x() / cell, p.y() / cell);
      if (std::find(spots.begin(), spots.end(), key) == spots.end()) {
        spots.push_back(key);
      }
    }
    if (spots.empty()) {
      continue;
    }
    const int count = std::min<int>(int(spots.size()), 48);
    const int cols = 8;
    const int rows = (count + cols - 1) / cols;
    const int tile = cell * scale;
    QImage sheet(cols * (tile + 4), rows * (tile + 4), QImage::Format_RGB32);
    sheet.fill(qRgb(80, 80, 80));
    QPainter painter(&sheet);
    for (int i = 0; i < count; ++i) {
      const QRect src(spots[i].x() * cell - cell / 2, spots[i].y() * cell - cell / 2, cell * 2, cell * 2);
      const QImage crop = out.copy(src).scaled(tile, tile, Qt::KeepAspectRatio, Qt::FastTransformation);
      painter.drawImage((i % cols) * (tile + 4), (i / cols) * (tile + 4), crop);
    }
    painter.end();
    sheet.save(QDir(outDir).filePath(name + "_spots.png"));
  }
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const QStringList args = app.arguments();
  if ((args.size() >= 4) && (args[1] == "run")) {
    int dpi = 200;
    for (const QString& arg : args.mid(4)) {
      if (arg.startsWith("--dpi=")) {
        dpi = arg.mid(6).toInt();
      }
    }
    return run(args[2], args[3], dpi);
  }
  if ((args.size() == 5) && (args[1] == "diff")) {
    return diff(args[2], args[3], args[4]);
  }
  std::cerr << "usage: despeckle_compare run <input folder> <output folder> [--dpi=200]\n"
               "       despeckle_compare diff <before folder> <after folder> <output folder>\n";
  return 1;
}
