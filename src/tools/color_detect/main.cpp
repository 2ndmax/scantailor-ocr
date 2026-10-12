// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: runs the color mode detection (ColorModeDetector, based on the detection of
// ScanTailor Spectre) on a folder of scans and writes the verdicts with their evidence to a CSV
// file, plus contact sheets.
//
// Usage: color_detect <input folder> <output folder> [--nosplit] [--zones] [--mincolor=3] [--minpicture=10]
// [--only=<name part>] [--inset=<percent>]
//
// Raw scans of two-page spreads are not split yet, so each scan is cropped to the bright page area
// (dark scanner borders removed) and cut in half across its longer side. Rotation hardly matters
// for the detector (histograms and tile regions). With --nosplit, single pages are analyzed as they
// are. In the program, the detection gets the content area of the page; to come close to it, the
// edges (book edge, gutter shadow, scanner border) are left out here.
// --zones uses our picture zone detection as a second opinion, --mincolor sets the minimum share of
// color outside pictures (in percent) for a color page.

#include <ColorModeDetector.h>
#include <ImageLoader.h>
#include <NullTaskStatus.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <iostream>
#include <vector>

namespace {
using Mode = ColorModeDetector::Mode;

// Crops dark scanner borders: removes edge rows and columns whose mean brightness is low.
QRect brightArea(const QImage& image) {
  const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  const int w = gray.width();
  const int h = gray.height();
  std::vector<double> colMean(w, 0.0);
  std::vector<double> rowMean(h, 0.0);
  for (int y = 0; y < h; ++y) {
    const uchar* line = gray.constScanLine(y);
    for (int x = 0; x < w; ++x) {
      colMean[x] += line[x];
      rowMean[y] += line[x];
    }
  }
  for (double& v : colMean) {
    v /= h;
  }
  for (double& v : rowMean) {
    v /= w;
  }
  const double dark = 100.0;
  int left = 0, right = w - 1, top = 0, bottom = h - 1;
  while (left < right && colMean[left] < dark) {
    ++left;
  }
  while (right > left && colMean[right] < dark) {
    --right;
  }
  while (top < bottom && rowMean[top] < dark) {
    ++top;
  }
  while (bottom > top && rowMean[bottom] < dark) {
    --bottom;
  }
  return QRect(QPoint(left, top), QPoint(right, bottom));
}

QColor verdictColor(Mode type) {
  switch (type) {
    case Mode::BlackWhite:
      return QColor(120, 120, 120);
    case Mode::Grayscale:
      return QColor(40, 90, 200);
    case Mode::Mixed:
      return QColor(230, 130, 0);
    case Mode::Color:
      return QColor(200, 30, 30);
  }
  return Qt::black;
}

struct Result {
  QString label;
  QImage thumb;
  Mode type;
};

void writeSheets(const std::vector<Result>& results, const QString& outDir) {
  const int cols = 6;
  const int rows = 5;
  const int cellW = 300;
  const int cellH = 330;
  const int perSheet = cols * rows;
  for (size_t start = 0; start < results.size(); start += perSheet) {
    QImage sheet(cols * cellW, rows * cellH, QImage::Format_RGB32);
    sheet.fill(Qt::white);
    QPainter p(&sheet);
    QFont font = p.font();
    font.setPixelSize(16);
    font.setBold(true);
    p.setFont(font);
    for (size_t i = start; i < std::min(results.size(), start + perSheet); ++i) {
      const int idx = static_cast<int>(i - start);
      const QRect cell((idx % cols) * cellW, (idx / cols) * cellH, cellW, cellH);
      const Result& r = results[i];
      p.fillRect(QRect(cell.left(), cell.top(), cellW, 26), verdictColor(r.type));
      p.setPen(Qt::white);
      p.drawText(QRect(cell.left() + 4, cell.top(), cellW - 8, 26), Qt::AlignVCenter | Qt::AlignLeft,
                 r.label + "  " + ColorModeDetector::modeToString(r.type));
      const QImage t = r.thumb.scaled(cellW - 8, cellH - 34, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      p.drawImage(cell.left() + (cellW - t.width()) / 2, cell.top() + 30, t);
    }
    p.end();
    sheet.save(QStringLiteral("%1/sheet_%2.png").arg(outDir).arg(start / perSheet + 1, 2, 10, QChar('0')));
  }
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);  // QPainter needs fonts.
  const QStringList args = app.arguments();
  if (args.size() < 3) {
    std::cerr << "Usage: color_detect <input folder> <output folder> [--nosplit] [--zones] [--mincolor=3] "
                 "[--minpicture=10] [--only=<name part>] [--inset=<percent>]\n";
    return 1;
  }
  const QDir inDir(args[1]);
  const QString outDir = args[2];
  const bool noSplit = args.contains("--nosplit");
  QString only;            // Only files whose name contains this.
  int insetOverride = -1;  // Share of each edge left out, in percent.
  ColorModeDetector::Options options;
  options.usePictureZones = args.contains("--zones");
  for (const QString& arg : args) {
    if (arg.startsWith("--only=")) {
      only = arg.mid(7);
    }
    if (arg.startsWith("--inset=")) {
      insetOverride = arg.mid(8).toInt();
    }
    if (arg.startsWith("--minpicture=")) {
      options.minPictureShare = arg.mid(13).toDouble() / 100;
    }
    if (arg.startsWith("--mincolor=")) {
      options.minColorShare = arg.mid(11).toDouble() / 100;
    }
  }
  QDir().mkpath(outDir);

  QFile csvFile(outDir + "/color_detect.csv");
  if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    std::cerr << "Cannot write " << csvFile.fileName().toStdString() << "\n";
    return 1;
  }
  QTextStream csv(&csvFile);
  csv << "file;part;mode;reason;spectre;darkRatio;midtoneRatio;lightRatio;colorFraction;toneTiles;"
         "largestRegionCoverage;pictureShare;largestPicture;colorInPictures;colorOutside;textOutside;picMean;picStdDev;"
         "picDark\n";

  const NullTaskStatus status;
  std::vector<Result> results;
  QStringList files = inDir.entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
  if (!only.isEmpty()) {
    files = files.filter(only);
  }
  int fileNo = 0;
  for (const QString& name : files) {
    ++fileNo;
    const QImage image = ImageLoader::load(inDir.filePath(name));
    if (image.isNull()) {
      std::cerr << "Cannot load " << name.toStdString() << "\n";
      continue;
    }
    const QRect area = brightArea(image);
    std::vector<std::pair<QString, QRect>> parts;
    if (noSplit) {
      parts.emplace_back("page", area);
    } else if (area.width() >= area.height()) {
      const int half = area.width() / 2;
      parts.emplace_back("left", QRect(area.left(), area.top(), half, area.height()));
      parts.emplace_back("right", QRect(area.left() + half, area.top(), area.width() - half, area.height()));
    } else {
      const int half = area.height() / 2;
      parts.emplace_back("top", QRect(area.left(), area.top(), area.width(), half));
      parts.emplace_back("bottom", QRect(area.left(), area.top() + half, area.width(), area.height() - half));
    }

    for (const auto& [partName, rect] : parts) {
      const QImage part = image.copy(rect);
      const int inset = (insetOverride >= 0) ? insetOverride : (noSplit ? 3 : 8);
      const QImage content = part.copy(part.rect().adjusted(part.width() * inset / 100, part.height() * inset / 100,
                                                            -part.width() * inset / 100, -part.height() * inset / 100));
      ColorModeDetector::Evidence ev;
      const Mode mode = ColorModeDetector::detect(content, options, status, &ev);
      csv << name << ';' << partName << ';' << ColorModeDetector::modeToString(mode) << ';' << ev.reason << ';'
          << LeptonicaDetector::colorTypeToString(ev.spectreVerdict) << ';' << ev.spectre.darkRatio << ';'
          << ev.spectre.midtoneRatio << ';' << ev.spectre.lightRatio << ';' << ev.spectre.colorFraction << ';'
          << ev.spectre.continuousToneTileCount << ';' << ev.spectre.largestRegionCoverage << ';' << ev.pictureShare
          << ';' << ev.largestPicture << ';' << ev.colorInPictures << ';' << ev.colorOutside << ';' << ev.textOutside
          << ';' << ev.largestPictureMean << ';' << ev.largestPictureStdDev << ';' << ev.largestPictureDark << '\n';
      results.push_back({QStringLiteral("%1 %2 (%3)")
                             .arg(fileNo)
                             .arg(partName)
                             .arg(QString::fromLatin1(LeptonicaDetector::colorTypeToString(ev.spectreVerdict))),
                         part.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation), mode});
    }
    std::cout << fileNo << "/" << files.size() << " " << name.toStdString() << "\n" << std::flush;
  }
  csv.flush();
  writeSheets(results, outDir);
  return 0;
}
