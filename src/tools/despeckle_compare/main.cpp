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
#include <ImageSizeLimit.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <filters/select_content/ContentBoxFinder.h>
#include <filters/select_content/DetectionOptions.h>

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <exception>
#include <iostream>
#include <vector>

#include "Bench.h"
#include "SpotSheet.h"
#include "tools/ToolImageSave.h"

using namespace imageproc;

namespace {
int run(const QString& inDir, const QString& outDir, const int dpi) {
  if (!QDir(inDir).exists()) {
    std::cerr << "Input folder not found: " << inDir.toStdString() << "\n";
    return 1;
  }
  if (!QDir().mkpath(outDir)) {
    std::cerr << "Cannot create " << outDir.toStdString() << "\n";
    return 1;
  }
  const QStringList files = QDir(inDir).entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    const QImage image = ImageLoader::load(QDir(inDir).filePath(file));
    if (image.isNull()) {
      std::cerr << "Cannot read " << file.toStdString() << "\n";
      continue;
    }
    const QString base = QDir(outDir).filePath(QFileInfo(file).completeBaseName());
    const QImage gray = toGrayscale(image);
    const BinaryImage binary(gray, BinaryThreshold::otsuThreshold(gray));
    tools::saveImage(binary.toQImage(), base + "_bin.png");
    for (const LevelName& level : kLevels) {
      const BinaryImage result = Despeckle::despeckle(binary, Dpi(dpi, dpi), level.level, NullTaskStatus());
      tools::saveImage(result.toQImage(), base + "_" + level.name + ".png");
    }
    std::cout << file.toStdString() << " done" << std::endl;
  }
  return tools::exitCodeOfSaves();
}

bool isBlack(const QImage& image, int x, int y) {
  return qGray(image.pixel(x, y)) < 128;
}

QString stripLevelSuffix(const QString& name) {
  for (const LevelName& level : kLevels) {
    const QString suffix = QLatin1Char('_') + QLatin1String(level.name);
    if (name.endsWith(suffix)) {
      return name.left(name.size() - suffix.size());
    }
  }
  return QString();
}

QImage buildDiffImage(const QImage& bin,
                      const QImage& before,
                      const QImage& after,
                      int& newlyRemoved,
                      int& newlyKept,
                      std::vector<QPoint>& changes) {
  QImage out(after.size(), QImage::Format_RGB32);
  out.fill(Qt::white);
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
  return out;
}

int diff(const QString& beforeDir, const QString& afterDir, const QString& outDir) {
  if (!QDir(beforeDir).exists()) {
    std::cerr << "Before folder not found: " << beforeDir.toStdString() << "\n";
    return 1;
  }
  if (!QDir(afterDir).exists()) {
    std::cerr << "After folder not found: " << afterDir.toStdString() << "\n";
    return 1;
  }
  if (!QDir().mkpath(outDir)) {
    std::cerr << "Cannot create " << outDir.toStdString() << "\n";
    return 1;
  }
  const QStringList files = QDir(afterDir).entryList({"*.png"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    const QString name = QFileInfo(file).completeBaseName();
    const QString baseName = stripLevelSuffix(name);
    if (baseName.isEmpty() || !QFileInfo::exists(QDir(beforeDir).filePath(file))) {
      continue;
    }
    const QString binFile = baseName + QLatin1String("_bin.png");
    const QImage bin(QDir(afterDir).filePath(binFile));
    const QImage before(QDir(beforeDir).filePath(file));
    const QImage after(QDir(afterDir).filePath(file));
    if (bin.isNull() || (before.size() != after.size()) || (bin.size() != after.size())) {
      std::cerr << "Skipping " << file.toStdString() << "\n";
      continue;
    }
    int newlyRemoved = 0;
    int newlyKept = 0;
    std::vector<QPoint> changes;
    const QImage out = buildDiffImage(bin, before, after, newlyRemoved, newlyKept, changes);
    tools::saveImage(out, QDir(outDir).filePath(name + "_diff.png"));
    std::cout << name.toStdString() << ": newly removed " << newlyRemoved << " px, newly kept " << newlyKept << " px\n";

    saveSpotSheet(out, changes, QDir(outDir).filePath(name + "_spots.png"));
  }
  return tools::exitCodeOfSaves();
}

// The content box detection uses the despeckling too: prints the box it finds on each scan.
int content(const QString& inDir, const int dpi) {
  if (!QDir(inDir).exists()) {
    std::cerr << "Input folder not found: " << inDir.toStdString() << "\n";
    return 1;
  }
  const QStringList files = QDir(inDir).entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
  for (const QString& file : files) {
    const QImage image = ImageLoader::load(QDir(inDir).filePath(file));
    if (image.isNull()) {
      std::cerr << "Cannot read " << file.toStdString() << "\n";
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

int printUsage() {
  std::cerr << "Usage: despeckle_compare run <input folder> <output folder> [--dpi=200]\n"
               "       despeckle_compare diff <before folder> <after folder> <output folder>\n"
               "       despeckle_compare bench <output folder> [--images]\n"
               "       despeckle_compare content <input folder> [--dpi=200]\n";
  return 1;
}

bool parseDpiCommand(const QStringList& rest, const int expectedPositional, QStringList& positional, int* dpi) {
  *dpi = 200;
  for (const QString& arg : rest) {
    if (arg.startsWith("--dpi=")) {
      bool ok = false;
      *dpi = arg.mid(6).toInt(&ok);
      if (!ok || (*dpi <= 0)) {
        std::cerr << "--dpi must be a positive number\n";
        return false;
      }
    } else if (arg.startsWith('-')) {
      std::cerr << "Unknown option: " << arg.toStdString() << "\n";
      return false;
    } else {
      positional << arg;
    }
  }
  return positional.size() == expectedPositional;
}

int runMain(int argc, char** argv) {
  // The benchmark renders text, which needs fonts and thus a GUI application, but no window.
  // Elsewhere there may be no display; on Windows the offscreen platform finds no fonts.
#ifndef Q_OS_WIN
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
  }
#endif
  QGuiApplication app(argc, argv);
  ImageSizeLimit::applyQtAllocationLimit();
  const QStringList args = app.arguments();
  if (args.size() < 2) {
    return printUsage();
  }
  const QString& cmd = args[1];
  const QStringList rest = args.mid(2);
  if (cmd == "run") {
    QStringList pos;
    int dpi = 200;
    if (!parseDpiCommand(rest, 2, pos, &dpi)) {
      return printUsage();
    }
    return run(pos[0], pos[1], dpi);
  }
  if (cmd == "content") {
    QStringList pos;
    int dpi = 200;
    if (!parseDpiCommand(rest, 1, pos, &dpi)) {
      return printUsage();
    }
    return content(pos[0], dpi);
  }
  if (cmd == "bench") {
    QStringList pos;
    bool images = false;
    for (const QString& arg : rest) {
      if (arg == "--images") {
        images = true;
      } else if (arg.startsWith('-')) {
        std::cerr << "Unknown option: " << arg.toStdString() << "\n";
        return printUsage();
      } else {
        pos << arg;
      }
    }
    if (pos.size() != 1) {
      return printUsage();
    }
    return bench(pos[0], images);
  }
  if (cmd == "diff") {
    for (const QString& arg : rest) {
      if (arg.startsWith('-')) {
        std::cerr << "Unknown option: " << arg.toStdString() << "\n";
        return printUsage();
      }
    }
    if (rest.size() != 3) {
      return printUsage();
    }
    return diff(rest[0], rest[1], rest[2]);
  }
  return printUsage();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    return runMain(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "Error: unknown exception\n";
    return 1;
  }
}
