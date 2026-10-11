// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: runs the color mode detection of ScanTailor Spectre (LeptonicaDetector) on a folder
// of scans and writes the verdicts with their evidence to a CSV file, plus contact sheets.
//
// Usage: color_detect <input folder> <output folder> [--whole] [--nosplit] [--masks] [--nozones] [--mincolor=1]
// [--mincolorpic=0.5] [--minpicture=25]
//
// Raw scans of two-page spreads are not split yet, so each scan is cropped to the bright page area
// (dark scanner borders removed) and cut in half across its longer side. Rotation hardly matters
// for the detector (histograms and tile regions). With --whole, the whole cropped scan is analyzed
// as well. With --nosplit, single pages are analyzed as they are.

#include <BinaryImage.h>
#include <ConnectivityMap.h>
#include <GrayImage.h>
#include <GrayRasterOp.h>
#include <Grayscale.h>
#include <ImageLoader.h>
#include <Morphology.h>
#include <Scale.h>
#include <SeedFill.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <iostream>
#include <vector>

#include "LeptonicaDetector.h"

namespace {
using ColorType = LeptonicaDetector::ColorType;

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

// The same steps as OutputGenerator::Processor::detectPictures() (picture zones of the Mixed mode).
struct CombineInverted {
  static uint8_t transform(uint8_t src, uint8_t dst) {
    const unsigned dilated = dst;
    const unsigned eroded = src;
    return static_cast<uint8_t>(255 - (255 - dilated) * eroded / 255);
  }
};

imageproc::GrayImage detectPictures(const imageproc::GrayImage& input300dpi, bool higherSensitivity) {
  using namespace imageproc;
  GrayImage stretched(stretchGrayRange(input300dpi, 0.01, 0.01));
  GrayImage eroded(erodeGray(stretched, QSize(3, 3), 0x00));
  GrayImage dilated(dilateGray(stretched, QSize(3, 3), 0xff));
  grayRasterOp<CombineInverted>(dilated, eroded);
  GrayImage grayGradient(dilated);
  GrayImage marker(erodeGray(grayGradient, QSize(35, 35), 0x00));
  seedFillGrayInPlace(marker, grayGradient, CONN8);
  GrayImage reconstructed(marker);
  grayRasterOp<GRopInvert<GRopSrc>>(reconstructed, reconstructed);
  GrayImage holesFilled(createFramedImage(reconstructed.size()));
  seedFillGrayInPlace(holesFilled, reconstructed, CONN8);
  if (higherSensitivity) {
    return stretchGrayRange(holesFilled, 5.0, 0.01);
  }
  return holesFilled;
}

struct PictureEvidence {
  double fraction = 0;         // Share of the part covered by picture areas.
  double largestFraction = 0;  // Share of the largest connected picture area.
  double colorInPictures = 0;  // Share of the part that is colored and inside picture areas.
  double colorOutside = 0;     // Share of the part that is colored outside picture areas
                               // (headings, boxes, thumb index tabs).
};

// A pixel counts as colored when its channels differ clearly (not gray) and it is not nearly black.
bool isColored(QRgb rgb) {
  const int r = qRed(rgb), g = qGreen(rgb), b = qBlue(rgb);
  const int mx = std::max({r, g, b});
  const int mn = std::min({r, g, b});
  return (mx - mn >= 40) && (mx >= 60);
}

PictureEvidence pictureEvidence(const QImage& part, const QString& maskFile = QString()) {
  using namespace imageproc;
  const double dpm = part.dotsPerMeterX() > 0 ? part.dotsPerMeterX() : 300 / 0.0254;
  const double scale = std::min(1.0, (300 / 0.0254) / dpm);
  const QSize size300(std::max(1, qRound(part.width() * scale)), std::max(1, qRound(part.height() * scale)));
  const GrayImage gray300(scaleToGray(GrayImage(part), size300));
  const BinaryImage pictures(detectPictures(gray300, false).toQImage(), BinaryThreshold(48));
  if (!maskFile.isEmpty()) {
    pictures.toQImage().save(maskFile);
  }

  // Light areas are pictures; BinaryImage stores them as WHITE.
  const BinaryImage pictureMask(pictures.inverted());
  PictureEvidence ev;
  const double total = double(pictureMask.width()) * pictureMask.height();
  ev.fraction = pictureMask.countBlackPixels() / total;

  if (part.format() != QImage::Format_Grayscale8 && part.format() != QImage::Format_Indexed8
      && part.format() != QImage::Format_Mono && part.format() != QImage::Format_MonoLSB) {
    const QImage color300
        = part.scaled(size300, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
    long inPictures = 0, outside = 0;
    for (int y = 0; y < color300.height(); ++y) {
      const auto* line = reinterpret_cast<const QRgb*>(color300.constScanLine(y));
      for (int x = 0; x < color300.width(); ++x) {
        if (isColored(line[x])) {
          ++(pictureMask.getPixel(x, y) == BLACK ? inPictures : outside);
        }
      }
    }
    ev.colorInPictures = inPictures / total;
    ev.colorOutside = outside / total;
  }
  ConnectivityMap cmap(pictureMask, CONN8);
  std::vector<int> areas(cmap.maxLabel() + 1, 0);
  const uint32_t* line = cmap.data();
  for (int y = 0; y < cmap.size().height(); ++y, line += cmap.stride()) {
    for (int x = 0; x < cmap.size().width(); ++x) {
      ++areas[line[x]];
    }
  }
  for (size_t label = 1; label < areas.size(); ++label) {
    ev.largestFraction = std::max(ev.largestFraction, areas[label] / total);
  }
  return ev;
}

QColor verdictColor(ColorType type) {
  switch (type) {
    case ColorType::BlackWhite:
      return QColor(120, 120, 120);
    case ColorType::Grayscale:
      return QColor(40, 90, 200);
    case ColorType::Mixed:
      return QColor(230, 130, 0);
    case ColorType::Color:
      return QColor(200, 30, 30);
  }
  return Qt::black;
}

struct Result {
  QString label;
  QImage thumb;
  ColorType type;
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
                 r.label + "  " + LeptonicaDetector::colorTypeToString(r.type));
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
    std::cerr << "Usage: color_detect <input folder> <output folder> [--whole] [--nosplit] [--masks] [--nozones] "
                 "[--mincolor=1] [--mincolorpic=0.5] [--minpicture=25]\n";
    return 1;
  }
  const QDir inDir(args[1]);
  const QString outDir = args[2];
  const bool whole = args.contains("--whole");
  const bool noSplit = args.contains("--nosplit");
  const bool saveMasks = args.contains("--masks");
  const bool useZones = !args.contains("--nozones");
  auto optionValue = [&args](const QString& name, double defaultValue) {
    for (const QString& arg : args) {
      if (arg.startsWith(name + "=")) {
        return arg.mid(name.size() + 1).toDouble();
      }
    }
    return defaultValue;
  };
  // Shares of the analyzed part, in percent on the command line.
  const double minColor = optionValue("--mincolor", 1.0) / 100;
  const double minColorInPictures = optionValue("--mincolorpic", 0.5) / 100;
  const double minPicture = optionValue("--minpicture", 25) / 100;
  QDir().mkpath(outDir);

  QFile csvFile(outDir + "/color_detect.csv");
  if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    std::cerr << "Cannot write " << csvFile.fileName().toStdString() << "\n";
    return 1;
  }
  QTextStream csv(&csvFile);
  csv << "file;part;verdict;reason;darkRatio;midtoneRatio;lightRatio;colorFraction;toneTiles;regions;"
         "largestRegionTiles;largestRegionCoverage;toneCoverage;pictures;largestPicture;colorInPictures;"
         "colorOutside;combined;combinedReason\n";

  std::vector<Result> results;
  const QStringList files = inDir.entryList({"*.tif", "*.tiff", "*.png", "*.jpg"}, QDir::Files, QDir::Name);
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
    if (whole) {
      parts.emplace_back("whole", area);
    }
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
      LeptonicaDetector::DetectionEvidence ev;
      const ColorType type = LeptonicaDetector::detectWithCastCompensation(part, 10, &ev);
      csv << name << ';' << partName << ';' << LeptonicaDetector::colorTypeToString(type) << ';'
          << LeptonicaDetector::classificationReason(ev) << ';' << ev.darkRatio << ';' << ev.midtoneRatio << ';'
          << ev.lightRatio << ';' << ev.colorFraction << ';' << ev.continuousToneTileCount << ';'
          << ev.continuousToneRegionCount << ';' << ev.largestRegionTileCount << ';' << ev.largestRegionCoverage << ';'
          << ev.continuousToneCoverage;
      // Our picture detection runs on the content area in the program, so leave out the edges
      // (book edge, gutter shadow, scanner border) here.
      const int inset = noSplit ? 3 : 8;
      const QImage inner = part.copy(part.rect().adjusted(part.width() * inset / 100, part.height() * inset / 100,
                                                          -part.width() * inset / 100, -part.height() * inset / 100));
      QString maskFile;
      if (saveMasks) {
        QDir().mkpath(outDir + "/masks");
        maskFile = QStringLiteral("%1/masks/%2_%3.png").arg(outDir).arg(fileNo, 3, 10, QChar('0')).arg(partName);
      }
      const PictureEvidence pic = pictureEvidence(inner, maskFile);

      // Combined verdict:
      // - Color only if enough color lies outside picture areas (headings, tabs and narrow boxes
      //   below the minimum share are ignored).
      // - Otherwise colored pictures make the page Mixed.
      // - Otherwise the page is judged again without color, with our picture zones as a second opinion.
      ColorType combined = type;
      QString why = "spectre";
      if (type == ColorType::Color && pic.colorOutside < minColor) {
        if (pic.colorInPictures >= minColorInPictures) {
          combined = ColorType::Mixed;
          why = "color_only_in_pictures";
        } else {
          LeptonicaDetector::DetectionEvidence grayEv;
          combined = LeptonicaDetector::detect(part.convertToFormat(QImage::Format_Grayscale8), 10, &grayEv);
          ev.darkRatio = grayEv.darkRatio;
          why = "too_little_color";
        }
      }
      if (useZones && combined == ColorType::BlackWhite && pic.largestFraction >= minPicture
          && pic.largestFraction < 0.95 && ev.darkRatio >= 0.01) {
        combined = ColorType::Mixed;
        why += "+picture_zone";
      }
      csv << ';' << pic.fraction << ';' << pic.largestFraction << ';' << pic.colorInPictures << ';' << pic.colorOutside
          << ';' << LeptonicaDetector::colorTypeToString(combined) << ';' << why << '\n';
      results.push_back({QStringLiteral("%1 %2 (%3)")
                             .arg(fileNo)
                             .arg(partName)
                             .arg(QString::fromLatin1(LeptonicaDetector::colorTypeToString(type))),
                         part.scaled(600, 600, Qt::KeepAspectRatio, Qt::SmoothTransformation), combined});
    }
    std::cout << fileNo << "/" << files.size() << " " << name.toStdString() << "\n" << std::flush;
  }
  csv.flush();
  writeSheets(results, outDir);
  return 0;
}
