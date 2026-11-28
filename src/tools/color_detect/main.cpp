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
#include <ImageSizeLimit.h>
#include <NullTaskStatus.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <vector>

#include "tools/ToolImageSave.h"

namespace {
using Mode = ColorModeDetector::Mode;

// Crops dark scanner borders: removes edge rows and columns whose mean brightness is low.
QRect brightArea(const QImage& image) {
  if (image.isNull() || image.size().isEmpty()) {
    return QRect();
  }
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
  while (left <= right && colMean[left] < dark) {
    ++left;
  }
  while (right >= left && colMean[right] < dark) {
    --right;
  }
  while (top <= bottom && rowMean[top] < dark) {
    ++top;
  }
  while (bottom >= top && rowMean[bottom] < dark) {
    --bottom;
  }
  if ((left > right) || (top > bottom)) {
    return QRect();
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
  Mode type = Mode::BlackWhite;
};

const int SHEET_COLS = 6;
const int SHEET_ROWS = 5;
const int CELL_WIDTH = 300;
const int CELL_HEIGHT = 330;
const size_t PER_SHEET = SHEET_COLS * SHEET_ROWS;

// One sheet of at most PER_SHEET results. Written as soon as it is full, so that the
// thumbnails of a long run aren't all kept in memory.
void writeSheet(const std::vector<Result>& results, const int sheetNo, const QString& outDir) {
  QImage sheet(SHEET_COLS * CELL_WIDTH, SHEET_ROWS * CELL_HEIGHT, QImage::Format_RGB32);
  sheet.fill(Qt::white);
  QPainter p(&sheet);
  QFont font = p.font();
  font.setPixelSize(16);
  font.setBold(true);
  p.setFont(font);
  for (size_t i = 0; i < results.size(); ++i) {
    const int idx = static_cast<int>(i);
    const QRect cell((idx % SHEET_COLS) * CELL_WIDTH, (idx / SHEET_COLS) * CELL_HEIGHT, CELL_WIDTH, CELL_HEIGHT);
    const Result& r = results[i];
    p.fillRect(QRect(cell.left(), cell.top(), CELL_WIDTH, 26), verdictColor(r.type));
    p.setPen(Qt::white);
    p.drawText(QRect(cell.left() + 4, cell.top(), CELL_WIDTH - 8, 26), Qt::AlignVCenter | Qt::AlignLeft,
               r.label + "  " + ColorModeDetector::modeToString(r.type));
    p.drawImage(cell.left() + (CELL_WIDTH - r.thumb.width()) / 2, cell.top() + 30, r.thumb);
  }
  p.end();
  tools::saveImage(sheet, QStringLiteral("%1/sheet_%2.png").arg(outDir).arg(sheetNo, 2, 10, QChar('0')));
}

// Reads a share given in percent (0 to 100) as a fraction. Returns false for anything else.
bool readPercent(const QString& value, double& fraction) {
  bool ok = false;
  const double percent = value.toDouble(&ok);
  if (!ok || !std::isfinite(percent) || (percent < 0.0) || (percent > 100.0)) {
    return false;
  }
  fraction = percent / 100.0;
  return true;
}
struct CliOptions {
  QString inDir;
  QString outDir;
  bool noSplit = false;
  QString only;            // Only files whose name contains this.
  int insetOverride = -1;  // Share of each edge left out, in percent.
  ColorModeDetector::Options detectorOptions;
};

bool parseOption(const QString& arg, CliOptions& cli, const char* const usage) {
  if (arg == "--nosplit") {
    cli.noSplit = true;
  } else if (arg == "--zones") {
    cli.detectorOptions.usePictureZones = true;
  } else if (arg.startsWith("--only=")) {
    cli.only = arg.mid(7);
  } else if (arg.startsWith("--inset=")) {
    // From 50 % on, the edges left out meet and no content is left.
    bool ok = false;
    cli.insetOverride = arg.mid(8).toInt(&ok);
    if (!ok || (cli.insetOverride < 0) || (cli.insetOverride >= 50)) {
      std::cerr << "--inset must be a whole number from 0 to 49\n";
      return false;
    }
  } else if (arg.startsWith("--minpicture=")) {
    if (!readPercent(arg.mid(13), cli.detectorOptions.minPictureShare)) {
      std::cerr << "--minpicture must be a number from 0 to 100\n";
      return false;
    }
  } else if (arg.startsWith("--mincolor=")) {
    if (!readPercent(arg.mid(11), cli.detectorOptions.minColorShare)) {
      std::cerr << "--mincolor must be a number from 0 to 100\n";
      return false;
    }
  } else {
    // A mistyped option would otherwise run with the default silently.
    std::cerr << "Unknown option: " << arg.toStdString() << "\n" << usage;
    return false;
  }
  return true;
}

bool parseCliArgs(const QStringList& args, CliOptions& cli) {
  const char* const usage
      = "Usage: color_detect <input folder> <output folder> [--nosplit] [--zones] [--mincolor=3] "
        "[--minpicture=10] [--only=<name part>] [--inset=<percent>]\n";
  QStringList positional;
  for (const QString& arg : args.mid(1)) {
    if (arg.startsWith('-')) {
      if (!parseOption(arg, cli, usage)) {
        return false;
      }
    } else {
      positional << arg;
    }
  }
  if (positional.size() != 2) {
    std::cerr << usage;
    return false;
  }
  cli.inDir = positional[0];
  cli.outDir = positional[1];
  return true;
}

std::vector<std::pair<QString, QRect>> splitParts(const QRect& area, const bool noSplit) {
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
  return parts;
}

void writeCsvRow(QTextStream& csv,
                 const QString& name,
                 const QString& partName,
                 const Mode mode,
                 const ColorModeDetector::Evidence& ev) {
  csv << name << ';' << partName << ';' << ColorModeDetector::modeToString(mode) << ';' << ev.reason << ';'
      << LeptonicaDetector::colorTypeToString(ev.spectreVerdict) << ';' << ev.spectre.darkRatio << ';'
      << ev.spectre.midtoneRatio << ';' << ev.spectre.lightRatio << ';' << ev.spectre.colorFraction << ';'
      << ev.spectre.continuousToneTileCount << ';' << ev.spectre.largestRegionCoverage << ';' << ev.pictureShare << ';'
      << ev.largestPicture << ';' << ev.colorInPictures << ';' << ev.colorOutside << ';' << ev.textOutside << ';'
      << ev.largestPictureMean << ';' << ev.largestPictureStdDev << ';' << ev.largestPictureDark << '\n';
}

void processPart(const QImage& image,
                 const QString& name,
                 const int fileNo,
                 const QString& partName,
                 const QRect& rect,
                 const CliOptions& cli,
                 const NullTaskStatus& status,
                 QTextStream& csv,
                 std::vector<Result>& results,
                 int& sheetNo) {
  const QImage part = image.copy(rect);
  const int inset = (cli.insetOverride >= 0) ? cli.insetOverride : (cli.noSplit ? 3 : 8);
  const QImage content = part.copy(part.rect().adjusted(part.width() * inset / 100, part.height() * inset / 100,
                                                        -part.width() * inset / 100, -part.height() * inset / 100));
  if (content.isNull()) {
    // A scan only a pixel or two wide (or nearly all dark border) leaves no content to analyze.
    std::cerr << name.toStdString() << " " << partName.toStdString() << ": no content left, skipped\n";
    return;
  }
  ColorModeDetector::Evidence ev;
  const Mode mode = ColorModeDetector::detect(content, cli.detectorOptions, status, &ev);
  writeCsvRow(csv, name, partName, mode, ev);
  results.push_back({QStringLiteral("%1 %2 (%3)")
                         .arg(fileNo)
                         .arg(partName)
                         .arg(QString::fromLatin1(LeptonicaDetector::colorTypeToString(ev.spectreVerdict))),
                     // Already the size of a cell of the sheet.
                     part.scaled(CELL_WIDTH - 8, CELL_HEIGHT - 34, Qt::KeepAspectRatio, Qt::SmoothTransformation),
                     mode});
  if (results.size() == PER_SHEET) {
    writeSheet(results, ++sheetNo, cli.outDir);
    results.clear();
  }
}

bool processScanFile(const QDir& inDir,
                     const QString& name,
                     const int fileNo,
                     const CliOptions& cli,
                     const NullTaskStatus& status,
                     QTextStream& csv,
                     std::vector<Result>& results,
                     int& sheetNo) {
  const QImage image = ImageLoader::load(inDir.filePath(name));
  if (image.isNull()) {
    std::cerr << "Cannot load " << name.toStdString() << "\n";
    return false;
  }
  const QRect area = brightArea(image);
  if (area.isEmpty()) {
    std::cerr << name.toStdString() << ": no bright area, skipped\n";
    return false;
  }
  for (const auto& [partName, rect] : splitParts(area, cli.noSplit)) {
    processPart(image, name, fileNo, partName, rect, cli, status, csv, results, sheetNo);
  }
  return true;
}

int runMain(int argc, char** argv) {
  QGuiApplication app(argc, argv);  // QPainter needs fonts.
  ImageSizeLimit::applyQtAllocationLimit();
  CliOptions cli;
  if (!parseCliArgs(app.arguments(), cli)) {
    return 1;
  }
  const QDir inDir(cli.inDir);
  if (!inDir.exists()) {
    std::cerr << "Input folder not found: " << inDir.path().toStdString() << "\n";
    return 1;
  }
  if (!QDir().mkpath(cli.outDir)) {
    std::cerr << "Cannot create " << cli.outDir.toStdString() << "\n";
    return 1;
  }

  QFile csvFile(cli.outDir + "/color_detect.csv");
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
  QStringList files
      = inDir.entryList({"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg", "*.jp2", "*.j2k"}, QDir::Files, QDir::Name);
  if (!cli.only.isEmpty()) {
    files = files.filter(cli.only);
  }
  int fileNo = 0;
  int sheetNo = 0;
  for (const QString& name : files) {
    ++fileNo;
    if (processScanFile(inDir, name, fileNo, cli, status, csv, results, sheetNo)) {
      std::cout << fileNo << "/" << files.size() << " " << name.toStdString() << "\n" << std::flush;
    }
  }
  csv.flush();
  if ((csv.status() != QTextStream::Ok) || !csvFile.flush() || (csvFile.error() != QFileDevice::NoError)) {
    std::cerr << "Cannot write " << csvFile.fileName().toStdString() << ": " << csvFile.errorString().toStdString()
              << "\n";
    return 1;
  }
  if (!results.empty()) {
    writeSheet(results, ++sheetNo, cli.outDir);
  }
  return tools::exitCodeOfSaves();
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
