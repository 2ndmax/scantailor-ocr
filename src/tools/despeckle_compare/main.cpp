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
//   despeckle_compare bench <output folder> [--images]
//     Measures the despeckling on generated pages with known text and noise (see Bench.h).
//   despeckle_compare content <input folder> [--dpi=200]
//     Prints the content box that the content detection (which despeckles too) finds on each scan.

#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <Despeckle.h>
#include <Dpi.h>
#include <FilterData.h>
#include <Grayscale.h>
#include <ImageLoader.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <filters/select_content/ContentBoxFinder.h>
#include <filters/select_content/DetectionOptions.h>

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <iostream>
#include <vector>

#include "Bench.h"
#include "SpotSheet.h"

using namespace imageproc;

namespace {
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

    saveSpotSheet(out, changes, QDir(outDir).filePath(name + "_spots.png"));
  }
  return 0;
}

// The content box detection uses the despeckling too: prints the box it finds on each scan.
int content(const QString& inDir, const int dpi) {
  const QStringList files = QDir(inDir).entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    const QImage image = ImageLoader::load(QDir(inDir).filePath(file));
    if (image.isNull()) {
      std::cerr << "cannot read " << file.toStdString() << "\n";
      continue;
    }
    const ImageTransformation xform(QRectF(image.rect()), Dpi(dpi, dpi));
    const FilterData data(FilterData(image), xform);
    const QRect box = select_content::ContentBoxFinder::findContentBox(NullTaskStatus(), data, xform.resultingRect(),
                                                                       select_content::DetectionOptions())
                          .toRect();
    std::cout << file.toStdString() << ": " << box.left() << "," << box.top() << " " << box.width() << "x"
              << box.height() << std::endl;
  }
  return 0;
}

int dpiArgument(const QStringList& args) {
  for (const QString& arg : args) {
    if (arg.startsWith("--dpi=")) {
      return arg.mid(6).toInt();
    }
  }
  return 200;
}
}  // namespace

int main(int argc, char** argv) {
  // The benchmark renders text, which needs fonts and thus a GUI application, but no window.
  // Elsewhere there may be no display; on Windows the offscreen platform finds no fonts.
#ifndef Q_OS_WIN
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  QGuiApplication app(argc, argv);
  const QStringList args = app.arguments();
  if ((args.size() >= 4) && (args[1] == "run")) {
    return run(args[2], args[3], dpiArgument(args.mid(4)));
  }
  if ((args.size() >= 3) && (args[1] == "content")) {
    return content(args[2], dpiArgument(args.mid(3)));
  }
  if ((args.size() >= 3) && (args[1] == "bench")) {
    return bench(args[2], args.mid(3).contains("--images"));
  }
  if ((args.size() == 5) && (args[1] == "diff")) {
    return diff(args[2], args[3], args[4]);
  }
  std::cerr << "usage: despeckle_compare run <input folder> <output folder> [--dpi=200]\n"
               "       despeckle_compare diff <before folder> <after folder> <output folder>\n"
               "       despeckle_compare bench <output folder> [--images]\n"
               "       despeckle_compare content <input folder> [--dpi=200]\n";
  return 1;
}
