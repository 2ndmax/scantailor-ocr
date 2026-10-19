// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: runs both methods of detecting the split line of two-page spreads (Edges and
// Spine line) on a folder of scans and writes one image per scan, plus a summary on stdout.
//
// Usage: split_compare <input folder> <output folder> [--rotate=<0|90|180|270>] [--only=<name part>]
//                      [--project=<project file>]
//
// With --project, only the files of the project are processed, each with the rotation of its Fix
// Orientation step; --rotate is the rotation of the other files.
//
// Each image is the 100 dpi grayscale image the detection works on, with the lines found by
// VertLineFinder (blue), the line chosen by Edges (red), the one chosen by Spine line (green),
// the center (gray) and the search window of the spine (yellow). Below it, the mean darkness of
// each column (black) and the score of the spine search (magenta, 0 to 1).
//
// The summary lists per scan the split line of each method (x at the middle row, in pixels of the
// rotated scan), the score of the best spine line and the best score more than 25 px (at 100 dpi)
// away from it, which shows how clear the decision was. A score below SpineLineFinder::MIN_SCORE
// counts as no spine.

#include <BinaryThreshold.h>
#include <Dpi.h>
#include <GrayImage.h>
#include <Grayscale.h>
#include <ImageLoader.h>
#include <ImageTransformation.h>
#include <OrthogonalRotation.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QGuiApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <vector>

#include "filters/page_split/PageLayout.h"
#include "filters/page_split/PageLayoutEstimator.h"
#include "filters/page_split/SpineLineFinder.h"
#include "filters/page_split/VertLineFinder.h"

using namespace page_split;

namespace {
Dpi dpiOf(const QImage& image) {
  const int x = static_cast<int>(std::lround(image.dotsPerMeterX() * 0.0254));
  const int y = static_cast<int>(std::lround(image.dotsPerMeterY() * 0.0254));
  return Dpi(x > 0 ? x : 300, y > 0 ? y : 300);
}

double xAtMiddle(const QLineF& line) {
  return 0.5 * (line.p1().x() + line.p2().x());
}

// The mean darkness of each column, the darkest of 3 pixels per row.
std::vector<double> columnDarkness(const imageproc::GrayImage& gray) {
  const int w = gray.width();
  const int h = gray.height();
  std::vector<double> result(w, 0.0);
  for (int x = 1; x + 1 < w; ++x) {
    long long sum = 0;
    for (int y = 0; y < h; ++y) {
      const uint8_t* row = gray.data() + y * gray.stride();
      sum += 255 - std::min({row[x - 1], row[x], row[x + 1]});
    }
    result[x] = static_cast<double>(sum) / h;
  }
  return result;
}

// The rotation set in the Fix Orientation step for each file of a project, by file name.
std::map<QString, int> rotationsFromProject(const QString& projectPath) {
  std::map<QString, int> rotations;
  QFile file(projectPath);
  QDomDocument doc;
  if (!file.open(QIODevice::ReadOnly) || !doc.setContent(&file)) {
    std::cerr << "Cannot read " << projectPath.toStdString() << "\n";
    return rotations;
  }
  const QDomElement root = doc.documentElement();
  std::map<QString, QString> fileNames;  // file id -> name
  for (QDomElement el = root.firstChildElement("files").firstChildElement("file"); !el.isNull();
       el = el.nextSiblingElement("file")) {
    fileNames[el.attribute("id")] = el.attribute("name");
  }
  std::map<QString, QString> imageFiles;  // image id -> file id
  for (QDomElement el = root.firstChildElement("images").firstChildElement("image"); !el.isNull();
       el = el.nextSiblingElement("image")) {
    imageFiles[el.attribute("id")] = el.attribute("fileId");
  }
  for (const auto& image : imageFiles) {
    rotations[fileNames[image.second]] = 0;
  }
  const QDomElement orientation = root.firstChildElement("filters").firstChildElement("fix-orientation");
  for (QDomElement el = orientation.firstChildElement("image"); !el.isNull(); el = el.nextSiblingElement("image")) {
    const QString name = fileNames[imageFiles[el.attribute("id")]];
    rotations[name] = el.firstChildElement("rotation").attribute("degrees").toInt();
  }
  return rotations;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const QStringList args = app.arguments();
  if (args.size() < 3) {
    std::cerr << "Usage: split_compare <input folder> <output folder> [--rotate=<degrees>] [--only=<name part>]"
                 " [--project=<file>]\n";
    return 1;
  }
  const QDir inDir(args[1]);
  const QDir outDir(args[2]);
  QDir().mkpath(outDir.path());
  int rotation = 0;
  QString only;
  QString projectPath;
  for (int i = 3; i < args.size(); ++i) {
    if (args[i].startsWith("--rotate=")) {
      rotation = args[i].mid(9).toInt();
    } else if (args[i].startsWith("--only=")) {
      only = args[i].mid(7);
    } else if (args[i].startsWith("--project=")) {
      projectPath = args[i].mid(10);
    }
  }

  QStringList files = inDir.entryList({"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg"}, QDir::Files, QDir::Name);
  std::map<QString, int> projectRotations;
  if (!projectPath.isEmpty()) {
    projectRotations = rotationsFromProject(projectPath);
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const QString& file) { return projectRotations.count(file) == 0; }),
                files.end());
  }
  std::cout << "file;width;edges_x;spine_x;spine_score;second_score;lines_x\n";
  for (const QString& file : files) {
    if (!only.isEmpty() && !file.contains(only)) {
      continue;
    }
    const QImage image = ImageLoader::load(inDir.filePath(file));
    if (image.isNull()) {
      std::cerr << "Cannot load " << file.toStdString() << "\n";
      continue;
    }
    // As the program does (FilterData): other gray formats are not handled everywhere.
    const QImage gray = imageproc::toGrayscale(image);

    ImageTransformation xform(QRectF(gray.rect()), dpiOf(image));
    OrthogonalRotation rot;
    const auto projectRotation = projectRotations.find(file);
    const int fileRotation = (projectRotation != projectRotations.end()) ? projectRotation->second : rotation;
    for (int deg = 0; deg < fileRotation; deg += 90) {
      rot.nextClockwiseDirection();
    }
    xform.setPreRotation(rot);

    imageproc::GrayImage gray100;
    QTransform outToDs;
    const std::vector<QLineF> lines = VertLineFinder::findLines(gray, xform, 8, nullptr, &gray100, &outToDs);

    const imageproc::BinaryThreshold threshold = imageproc::BinaryThreshold::otsuThreshold(gray);
    const PageLayout edges
        = PageLayoutEstimator::estimatePageLayout(TWO_PAGES, EDGES_DETECTION, gray, xform, threshold);
    const PageLayout spine
        = PageLayoutEstimator::estimatePageLayout(TWO_PAGES, SPINE_LINE_DETECTION, gray, xform, threshold);

    const QRectF virtualRect = xform.resultingRect();
    std::vector<double> profile;
    double score = 0.0;
    SpineLineFinder::findSpine(gray100, outToDs, virtualRect, nullptr, &profile, &score);
    const auto bestIt = std::max_element(profile.begin(), profile.end());
    const double bestX = static_cast<double>(bestIt - profile.begin());
    double second = 0.0;
    for (size_t x = 0; x < profile.size(); ++x) {
      if (std::fabs(x - bestX) > 25) {
        second = std::max(second, profile[x]);
      }
    }

    std::cout << file.toStdString() << ";" << virtualRect.width() << ";" << std::lround(xAtMiddle(edges.cutterLine(0)))
              << ";" << std::lround(xAtMiddle(spine.cutterLine(0))) << ";" << score << ";" << second << ";";
    for (const QLineF& line : lines) {
      std::cout << std::lround(xAtMiddle(line)) << " ";
    }
    std::cout << "\n";

    // The picture: the 100 dpi image above, the column darkness and the spine score below.
    const int plotHeight = 160;
    const QImage base = gray100.toQImage();
    QImage visual(base.width(), base.height() + plotHeight, QImage::Format_RGB32);
    visual.fill(Qt::white);
    QPainter painter(&visual);
    painter.drawImage(0, 0, base);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto toDs = [&](const QLineF& line) { return outToDs.map(line); };
    const QLineF centerLine(virtualRect.center().x(), virtualRect.top(), virtualRect.center().x(),
                            virtualRect.bottom());
    const double centerDs = toDs(centerLine).center().x();
    const double halfWindowDs = SpineLineFinder::CENTER_WINDOW_FRACTION * gray100.width();

    painter.setPen(QPen(QColor(255, 200, 0, 140), 1.0));
    painter.drawLine(QLineF(centerDs - halfWindowDs, 0, centerDs - halfWindowDs, base.height()));
    painter.drawLine(QLineF(centerDs + halfWindowDs, 0, centerDs + halfWindowDs, base.height()));
    painter.setPen(QPen(QColor(128, 128, 128, 160), 1.0, Qt::DashLine));
    painter.drawLine(toDs(centerLine));
    painter.setPen(QPen(QColor(0, 90, 255, 200), 1.0));
    for (const QLineF& line : lines) {
      painter.drawLine(toDs(line));
    }
    painter.setPen(QPen(QColor(255, 0, 0, 200), 3.0));
    painter.drawLine(toDs(edges.cutterLine(0)));
    painter.setPen(QPen(QColor(0, 200, 0, 200), 2.0));
    painter.drawLine(toDs(spine.cutterLine(0)));

    const int plotTop = base.height();
    painter.fillRect(0, plotTop, visual.width(), plotHeight, QColor(245, 245, 245));
    painter.setPen(QPen(QColor(200, 200, 200), 1.0));
    for (int level = 50; level < 255; level += 50) {
      const double y = plotTop + plotHeight - level * plotHeight / 255.0;
      painter.drawLine(QLineF(0, y, visual.width(), y));
    }
    const std::vector<double> darkness = columnDarkness(gray100);
    painter.setPen(QPen(Qt::black, 1.0));
    for (size_t x = 1; x < darkness.size(); ++x) {
      painter.drawLine(QLineF(x - 1, plotTop + plotHeight - darkness[x - 1] * plotHeight / 255.0, x,
                              plotTop + plotHeight - darkness[x] * plotHeight / 255.0));
    }
    painter.setPen(QPen(QColor(220, 0, 220), 1.0));
    for (size_t x = 1; x < profile.size(); ++x) {
      painter.drawLine(QLineF(x - 1, plotTop + plotHeight - profile[x - 1] * plotHeight, x,
                              plotTop + plotHeight - profile[x] * plotHeight));
    }
    painter.end();

    QString outName = file;
    outName.truncate(outName.lastIndexOf('.'));
    visual.save(outDir.filePath(outName + ".png"));
  }
  return 0;
}
