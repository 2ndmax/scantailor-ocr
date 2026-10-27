// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: measures the automatic oblique (shear) detection on synthetic test pages with a
// known angle. Each page is measured twice, the second time moved by a fraction of a pixel, which
// shows how stable the measurement is. The methods compared are listed in METHODS; the first is
// the one the program uses.
//
// Usage: oblique_compare <output folder> [--pages=<n>] [--seed=<n>] [--jobs=<n>]
//        oblique_compare <output folder> --images=<folder> [--jobs=<n>]
//
// With --images, the real scans in the folder are measured as they are and with known shears added
// (real_scans.html, .txt, .csv), since their own shear is unknown.
//
// The pages come from a fixed pseudo-random sequence (see TestPage.h): the same seed gives the
// same pages. The output folder gets report.html (with images of the most notable cases in
// images/), results.csv (one row per page and method) and summary.txt (the tables
// and the conclusions).

#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <Dpi.h>
#include <Dpm.h>
#include <GrayImage.h>
#include <ImageLoader.h>
#include <NullTaskStatus.h>
#include <SkewFinder.h>
#include <Transform.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFontInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QTextStream>
#include <QTransform>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "Methods.h"
#include "Report.h"
#include "TestPage.h"
#include "filters/deskew/Task.h"

using namespace compare;
using namespace imageproc;

namespace {
const double DEG2RAD = M_PI / 180.0;
const int NUM_CASES = 10;

/** Not a multiple of half a pixel, so that the sampling grid really moves. */
const QPointF SHIFT(0.37, 0.61);

/** What a method gets of a scan: the cleaned-up page of the deskew step, the gray scan, the deskew angle, the
 * resolution. */
struct ScanInput {
  SkewFinder& skewFinder;
  const BinaryImage& cleaned;
  const QImage& gray;
  double deskewAngleDeg;
  int dpi;
};

/** An oblique method: the angle in the sense of deskew::Task::findObliqueAngle(), or nothing. */
struct Method {
  QString name;
  std::function<Finding(const ScanInput&)> find;
};

const std::vector<Method>& methods() {
  static const std::vector<Method> METHODS
      = {{QStringLiteral("program"),
          [](const ScanInput& in) {
            return Finding{deskew::Task::findObliqueAngle(NullTaskStatus(), in.cleaned, in.deskewAngleDeg, 1.0),
                           std::nullopt};
          }},
         {QStringLiteral("(c2) edges"), [](const ScanInput& in) { return edgeFit(in.cleaned, in.deskewAngleDeg); }},
         {QStringLiteral("(d) stroke direction"),
          [](const ScanInput& in) { return strokeDirection(in.gray, in.deskewAngleDeg, in.dpi); }},
         {QStringLiteral("(e) edges, else stroke direction"),
          [](const ScanInput& in) { return edgesThenStrokes(in.cleaned, in.gray, in.deskewAngleDeg, in.dpi); }}};
  return METHODS;
}

struct Options {
  QString outDir;
  int pages = 3 * PAGES_PER_ROUND;
  uint32_t seed = 20261026;
  int jobs = 1;
  /** With --images: real scans from this folder instead of synthetic pages. */
  QString imagesDir;
};

/** The black-and-white page the deskew step measures on, cleaned up like there. */
BinaryImage cleanedPage(const QImage& scan, const int dpi) {
  BinaryImage image(scan, BinaryThreshold::otsuThreshold(scan));
  deskew::Task::cleanup(NullTaskStatus(), image, Dpi(dpi, dpi));
  return image;
}

/** The deskew angle of a scan and what each method finds on it. */
struct ScanMeasurement {
  std::optional<double> rotation;
  /** In the order of methods(). */
  std::vector<Finding> findings;
  /** The time each method took, in the order of methods(). */
  std::vector<double> milliseconds;
};

/** Like the program: the deskew angle first, then each oblique method on the page turned by it. */
ScanMeasurement measureAll(const QImage& scan, const int dpi) {
  const BinaryImage image(cleanedPage(scan, dpi));
  SkewFinder skewFinder;
  ScanMeasurement result;
  result.rotation = deskew::Task::findDeskewAngle(skewFinder, image, deskew::DETECT_CONTENT);
  for (const Method& method : methods()) {
    QElapsedTimer timer;
    timer.start();
    result.findings.push_back(method.find({skewFinder, image, scan, result.rotation.value_or(0.0), dpi}));
    result.milliseconds.push_back(timer.nsecsElapsed() / 1e6);
  }
  return result;
}

/** \p shifted selects which of the two results of each method is filled. */
void measureScan(const QImage& scan, const int dpi, const bool shifted, PageResult& result) {
  const ScanMeasurement measured(measureAll(scan, dpi));
  if (!shifted) {
    result.rotation = measured.rotation;
  }
  for (size_t m = 0; m < methods().size(); ++m) {
    const Finding& finding = measured.findings[m];
    if (shifted) {
      result.methods[m].shearShifted = finding.angle;
    } else {
      result.methods[m].shear = finding.angle;
      result.methods[m].confidence = finding.confidence;
      result.methods[m].milliseconds = measured.milliseconds[m];
    }
  }
}

PageResult measure(const PageSpec& spec, const Options& options) {
  const QImage upright(renderPage(spec, options.seed));
  PageResult result;
  result.spec = spec;
  result.methods.resize(methods().size());
  measureScan(scannedPage(upright, spec, options.seed), spec.dpi, false, result);
  measureScan(scannedPage(upright, spec, options.seed, SHIFT), spec.dpi, true, result);
  return result;
}

QString number(const double value, const int decimals = 2) {
  return QString::number(value, 'f', decimals);
}

QString angleText(const std::optional<double>& angle) {
  return angle ? number(*angle) + QStringLiteral("°") : QStringLiteral("no angle");
}

QString pageText(const PageSpec& s) {
  return QStringLiteral("page %1 · %2 dpi · %3 · %4 · %5 · disturbance: %6")
             .arg(s.index)
             .arg(s.dpi)
             .arg(toString(s.text), toString(s.layout), toString(s.alignment), toString(s.disturbance))
         + ((s.keystone != 0) ? QStringLiteral(" · keystone %1 %").arg(number(100 * s.keystone, 1)) : QString())
         + QStringLiteral(" · %1 · %2")
               .arg(s.serif ? QStringLiteral("serif") : QStringLiteral("sans serif"), toString(s.italic))
         + (s.headerFooter ? QStringLiteral(" · header and footer") : QString())
         + (s.indents ? QStringLiteral(" · indents") : QString()) + (s.lists ? QStringLiteral(" · lists") : QString());
}

/** The direction in the scan of the upright vertical, for the given angles. */
QPointF vertical(const double rotationDeg, const double shearDeg) {
  QTransform rotation;
  rotation.rotate(-rotationDeg);
  return (QTransform(1, 0, std::tan(shearDeg * DEG2RAD), 1, 0, 0) * rotation).map(QPointF(0, 1));
}

/** The scan, scaled down, with lines through it along the true and the corrected vertical. */
QImage angleImage(const QImage& scan, const QPointF& truth, const QPointF& found) {
  QImage image(scan.scaledToHeight(800, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  const double length = image.width() + image.height();
  const std::pair<QColor, QPointF> lines[] = {{QColor(20, 170, 40), truth}, {QColor(220, 30, 30), found}};
  for (const auto& line : lines) {
    painter.setPen(QPen(line.first, 1.5));
    const QPointF unit(line.second / std::hypot(line.second.x(), line.second.y()));
    for (const double t : {0.25, 0.5, 0.75}) {
      const QPointF anchor(t * image.width(), 0.5 * image.height());
      painter.drawLine(anchor - length * unit, anchor + length * unit);
    }
  }
  return image;
}

std::vector<CaseImage> caseImages(const std::vector<PageResult>& results, const Options& options) {
  std::vector<CaseImage> cases;
  for (size_t m = 0; m < methods().size(); ++m) {
    const int method = static_cast<int>(m);
    std::vector<const PageResult*> sorted;
    for (const PageResult& r : results) {
      sorted.push_back(&r);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [method](const PageResult* a, const PageResult* b) {
      return std::abs(a->residualShear(method)) > std::abs(b->residualShear(method));
    });
    sorted.resize(std::min<size_t>(sorted.size(), NUM_CASES));
    for (const PageResult* r : sorted) {
      const QImage upright(renderPage(r->spec, options.seed));
      const double rotation = r->rotation.value_or(0.0);
      const QImage image(angleImage(scannedPage(upright, r->spec, options.seed),
                                    vertical(r->spec.rotationDeg, r->spec.shearDeg),
                                    vertical(rotation, r->methods[m].shear.value_or(0.0))));
      const QString file(QStringLiteral("images/m%1_%2.png").arg(m).arg(r->spec.index, 4, 10, QLatin1Char('0')));
      if (!image.save(options.outDir + QLatin1Char('/') + file)) {
        std::cerr << "Cannot write " << qPrintable(file) << std::endl;
        continue;
      }
      const QString caption(pageText(r->spec)
                            + QStringLiteral(": shear true %1, detected %2, shifted %3 (rotation true %4, "
                                             "detected %5)")
                                  .arg(angleText(r->spec.shearDeg), angleText(r->methods[m].shear),
                                       angleText(r->methods[m].shearShifted), angleText(r->spec.rotationDeg),
                                       angleText(r->rotation)));
      cases.push_back({method, file, caption});
    }
  }
  return cases;
}

// Real scans. Their true shear is unknown, so each is measured as it is and then again with a known
// shear added: the difference between the two must be the added shear.

const double ADDED_SHEARS_DEG[] = {-3.0, -1.5, -0.5, 0.5, 1.5, 3.0};

/** The gray page sheared about its centre, as a scanner would: lower lines move right for \p shearDeg > 0. */
QImage withAddedShear(const QImage& gray, const double shearDeg) {
  const QPointF center(0.5 * gray.width(), 0.5 * gray.height());
  const QTransform xform(QTransform::fromTranslate(-center.x(), -center.y())
                         * QTransform(1, 0, std::tan(shearDeg * DEG2RAD), 1, 0, 0)
                         * QTransform::fromTranslate(center.x(), center.y()));
  const QRect dstRect(xform.mapRect(QRectF(gray.rect())).toAlignedRect());
  return transformToGray(gray, xform, dstRect, OutsidePixels::assumeColor(Qt::white)).toQImage();
}

struct RealPage {
  QString name;
  int dpi = 0;
  bool dpiAssumed = false;
  QSize size;
  ScanMeasurement asIs;
  /** In the order of ADDED_SHEARS_DEG. */
  std::vector<ScanMeasurement> added;
};

RealPage measureRealPage(const QString& path) {
  RealPage page;
  page.name = QFileInfo(path).fileName();
  const QImage loaded(ImageLoader::load(path));
  if (loaded.isNull()) {
    return page;
  }
  page.size = loaded.size();
  const Dpm dpm(loaded);
  page.dpi = dpm.isNull() ? 0 : Dpi(dpm).horizontal();
  if (page.dpi <= 0) {
    page.dpi = 300;
    page.dpiAssumed = true;
  }
  const QImage gray(GrayImage(loaded).toQImage());
  page.asIs = measureAll(gray, page.dpi);
  for (const double shearDeg : ADDED_SHEARS_DEG) {
    page.added.push_back(measureAll(withAddedShear(gray, shearDeg), page.dpi));
  }
  return page;
}

/** The scan, scaled down, with lines along the vertical that each method would make of it. */
QImage realPageImage(const QString& path, const RealPage& page) {
  const QImage loaded(ImageLoader::load(path));
  QImage image(loaded.scaledToHeight(std::min(loaded.height(), 2000), Qt::SmoothTransformation)
                   .convertToFormat(QImage::Format_RGB32));
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  const double length = image.width() + image.height();
  static const QColor COLORS[] = {QColor(150, 150, 150), QColor(40, 90, 230), QColor(240, 140, 0), QColor(220, 30, 30)};
  for (size_t m = 0; m < methods().size(); ++m) {
    const std::optional<double>& angle = page.asIs.findings[m].angle;
    if (!angle) {
      continue;
    }
    painter.setPen(QPen(COLORS[m % std::size(COLORS)], 1.5));
    const QPointF v(vertical(page.asIs.rotation.value_or(0.0), *angle));
    const QPointF unit(v / std::hypot(v.x(), v.y()));
    for (const double t : {0.2, 0.5, 0.8}) {
      const QPointF anchor(t * image.width(), 0.5 * image.height());
      painter.drawLine(anchor - length * unit, anchor + length * unit);
    }
  }
  return image;
}

/**
 * The page side by side: only deskewed, then straightened with the angle of each method. A fine
 * vertical grid lies over each panel; on the right one the text edges and stems run along it.
 */
QImage comparisonImage(const QString& path, const RealPage& page) {
  const QImage gray(GrayImage(ImageLoader::load(path)).toQImage());
  const double rotation = page.asIs.rotation.value_or(0.0);
  std::vector<std::pair<QString, QImage>> panels;
  panels.emplace_back(QStringLiteral("rotated only"), straightened(gray, rotation, 0.0));
  // The combination is one of the others; it is named in the caption.
  for (size_t m = 0; m + 1 < methods().size(); ++m) {
    const std::optional<double>& angle = page.asIs.findings[m].angle;
    if (angle) {
      panels.emplace_back(methods()[m].name + QStringLiteral(": ") + angleText(angle),
                          straightened(gray, rotation, *angle));
    } else {
      panels.emplace_back(methods()[m].name + QStringLiteral(": no angle"), QImage());
    }
  }

  const int panelHeight = std::min(gray.height(), 1600);
  const int labelHeight = 28;
  const int gap = 12;
  int panelWidth = 0;
  for (auto& [label, image] : panels) {
    if (!image.isNull()) {
      image = image.scaledToHeight(panelHeight, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGB32);
      panelWidth = std::max(panelWidth, image.width());
    }
  }
  QImage result(static_cast<int>(panels.size()) * (panelWidth + gap), panelHeight + labelHeight, QImage::Format_RGB32);
  result.fill(Qt::white);
  QPainter painter(&result);
  QFont font(painter.font());
  font.setPixelSize(18);
  painter.setFont(font);
  for (size_t i = 0; i < panels.size(); ++i) {
    const int left = static_cast<int>(i) * (panelWidth + gap);
    const auto& [label, image] = panels[i];
    painter.setPen(Qt::black);
    painter.drawText(QRect(left, 0, panelWidth, labelHeight), Qt::AlignCenter, label);
    if (image.isNull()) {
      continue;
    }
    painter.drawImage(left, labelHeight, image);
    painter.setPen(QPen(QColor(40, 90, 230, 110), 1));
    for (int x = 20; x < image.width(); x += 20) {
      painter.drawLine(left + x, labelHeight, left + x, labelHeight + image.height());
    }
  }
  return result;
}

bool writeRealReport(const Options& options,
                     const std::vector<QString>& paths,
                     const std::vector<RealPage>& pages,
                     QString* error) {
  QString text;
  QTextStream out(&text);
  QString csv;
  QTextStream csvOut(&csv);
  csvOut << "file;dpi;method;added;detected;detected_without_added;error\n";
  out << "Real scans: " << pages.size() << " pages from " << options.imagesDir << "\n";
  out << "Detected as supplied (the angle that straightens the page; rotation detected):\n";
  for (const RealPage& page : pages) {
    out << "- " << page.name << " (" << page.size.width() << " x " << page.size.height() << ", " << page.dpi << " dpi"
        << (page.dpiAssumed ? ", assumed" : "") << "), rotation " << angleText(page.asIs.rotation);
    for (size_t m = 0; m < methods().size(); ++m) {
      const Finding& finding = page.asIs.findings[m];
      out << " | " << methods()[m].name << ": " << angleText(finding.angle);
      if (finding.confidence) {
        out << " (confidence " << number(*finding.confidence) << ")";
      }
    }
    out << "\n";
  }
  out << "\nAdded shear (" << std::size(ADDED_SHEARS_DEG)
      << " angles per page): error = (detected with added - detected without) - added\n";
  for (size_t m = 0; m < methods().size(); ++m) {
    double sum = 0;
    double worst = 0;
    int count = 0;
    int missing = 0;
    for (const RealPage& page : pages) {
      const std::optional<double>& base = page.asIs.findings[m].angle;
      for (size_t s = 0; s < page.added.size(); ++s) {
        const std::optional<double>& found = page.added[s].findings[m].angle;
        std::optional<double> err;
        if (base && found) {
          err = (*found - *base) - ADDED_SHEARS_DEG[s];
          sum += std::abs(*err);
          worst = std::max(worst, std::abs(*err));
          ++count;
        } else {
          ++missing;
        }
        csvOut << page.name << ";" << page.dpi << ";" << methods()[m].name << ";" << number(ADDED_SHEARS_DEG[s], 1)
               << ";" << (found ? number(*found, 4) : QString()) << ";" << (base ? number(*base, 4) : QString()) << ";"
               << (err ? number(*err, 4) : QString()) << "\n";
      }
    }
    out << "- " << methods()[m].name << ": ";
    if (count > 0) {
      out << "mean error " << number(sum / count) << "°, worst " << number(worst) << "°";
    } else {
      out << "no values";
    }
    out << "; no angle (before or after) " << missing << " of " << (count + missing) << "\n";
  }

  QString html;
  QTextStream htmlOut(&html);
  htmlOut << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\"><title>Shear on Real Scans"
             "</title><style>body{font-family:sans-serif;margin:24px;color:#222;background:#fff}pre{font-size:13px;"
             "white-space:pre-wrap}figure{display:inline-block;margin:8px;vertical-align:top}figure img{border:1px "
             "solid #ccc;max-width:100%}figcaption{font-size:13px}</style></head><body>\n<h1>Shear on Real Scans"
             "</h1>\n<p>Lines: the vertical after the detected angle; gray the program, blue (c2), orange (d), "
             "red (e). "
             "A missing color means that the method found no angle. Where the lines run parallel to the vertical "
             "edges of the text, the angle is right.</p>\n<pre>"
          << text.toHtmlEscaped() << "</pre>\n";
  for (size_t i = 0; i < pages.size(); ++i) {
    const QString file(QStringLiteral("images/real_%1.png").arg(i + 1));
    if (!realPageImage(paths[i], pages[i]).save(options.outDir + QLatin1Char('/') + file)) {
      *error = QStringLiteral("Cannot write ") + file;
      return false;
    }
    htmlOut << "<figure><img src=\"" << file << "\" alt=\"\"><figcaption>" << pages[i].name.toHtmlEscaped()
            << "</figcaption></figure>\n";
  }
  htmlOut << "<h2>Comparison per Method</h2>\n<p>Each page rotated and unsheared with the angle of the method. "
             "The blue grid is vertical: where the left and right text edges and the letter stems run parallel "
             "to the grid, the angle is right. Open the image at full size for a close look.</p>\n";
  const int combined = static_cast<int>(methods().size()) - 1;
  for (size_t i = 0; i < pages.size(); ++i) {
    const QString file(QStringLiteral("images/comparison_%1.png").arg(i + 1));
    if (!comparisonImage(paths[i], pages[i]).save(options.outDir + QLatin1Char('/') + file)) {
      *error = QStringLiteral("Cannot write ") + file;
      return false;
    }
    htmlOut << "<figure><a href=\"" << file << "\"><img src=\"" << file << "\" alt=\"\"></a><figcaption>"
            << pages[i].name.toHtmlEscaped() << ": " << methods()[combined].name.toHtmlEscaped() << " gives "
            << angleText(pages[i].asIs.findings[combined].angle) << "</figcaption></figure>\n";
  }
  htmlOut << "</body></html>\n";

  const std::pair<QString, const QString*> files[] = {{QStringLiteral("real_scans.txt"), &text},
                                                      {QStringLiteral("real_scans.csv"), &csv},
                                                      {QStringLiteral("real_scans.html"), &html}};
  for (const auto& [name, content] : files) {
    QFile file(options.outDir + QLatin1Char('/') + name);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      *error = QStringLiteral("Cannot write %1: %2").arg(file.fileName(), file.errorString());
      return false;
    }
    file.write(content->toUtf8());
  }
  return true;
}

int runRealImages(const Options& options) {
  const QDir dir(options.imagesDir);
  std::vector<QString> paths;
  for (const QString& name : dir.entryList({QStringLiteral("*.tif"), QStringLiteral("*.tiff"), QStringLiteral("*.png"),
                                            QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.jp2")},
                                           QDir::Files, QDir::Name)) {
    paths.push_back(dir.filePath(name));
  }
  if (paths.empty()) {
    std::cerr << "No images in " << qPrintable(options.imagesDir) << std::endl;
    return 1;
  }
  std::vector<RealPage> pages(paths.size());
  std::atomic<int> next(0);
  std::vector<std::thread> workers;
  for (int j = 0; j < options.jobs; ++j) {
    workers.emplace_back([&]() {
      for (int i = next++; i < static_cast<int>(paths.size()); i = next++) {
        pages[i] = measureRealPage(paths[i]);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  for (size_t i = 0; i < pages.size(); ++i) {
    if (pages[i].asIs.findings.empty()) {
      std::cerr << "Cannot load " << qPrintable(paths[i]) << std::endl;
      return 1;
    }
  }
  QString error;
  if (!writeRealReport(options, paths, pages, &error)) {
    std::cerr << qPrintable(error) << std::endl;
    return 1;
  }
  std::cerr << "done: " << qPrintable(QDir(options.outDir).absolutePath()) << "/real_scans.html" << std::endl;
  return 0;
}

bool parseOptions(const QStringList& args, Options& options) {
  options.jobs = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 6);
  for (int i = 1; i < args.size(); ++i) {
    const QString& arg = args[i];
    bool ok = true;
    if (arg.startsWith(QLatin1String("--pages="))) {
      options.pages = arg.mid(8).toInt(&ok);
      ok = ok && options.pages > 0;
    } else if (arg.startsWith(QLatin1String("--seed="))) {
      options.seed = arg.mid(7).toUInt(&ok);
    } else if (arg.startsWith(QLatin1String("--images="))) {
      options.imagesDir = arg.mid(9);
      ok = !options.imagesDir.isEmpty();
    } else if (arg.startsWith(QLatin1String("--jobs="))) {
      options.jobs = arg.mid(7).toInt(&ok);
      ok = ok && options.jobs > 0;
    } else if (!arg.startsWith(QLatin1String("--")) && options.outDir.isEmpty()) {
      options.outDir = arg;
    } else {
      ok = false;
    }
    if (!ok) {
      std::cerr << "Invalid argument: " << qPrintable(arg) << std::endl;
      return false;
    }
  }
  return !options.outDir.isEmpty();
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  Options options;
  if (!parseOptions(app.arguments(), options)) {
    std::cerr << "Usage: oblique_compare <output folder> [--pages=<n>] [--seed=<n>] [--jobs=<n>]\n"
                 "       oblique_compare <output folder> --images=<folder> [--jobs=<n>]\n"
                 "Defaults: 243 pages (every combination three times), seed 20261026, half of the processor cores "
                 "(at most 6)."
              << std::endl;
    return 1;
  }
  if (!QDir().mkpath(options.outDir + QStringLiteral("/images"))) {
    std::cerr << "Cannot create " << qPrintable(options.outDir) << std::endl;
    return 1;
  }
  if (!options.imagesDir.isEmpty()) {
    return runRealImages(options);
  }
  // Images of an earlier run would stay next to the new ones.
  QDir images(options.outDir + QStringLiteral("/images"));
  for (const QString& file : images.entryList({QStringLiteral("m*_*.png")})) {
    images.remove(file);
  }


  QElapsedTimer timer;
  timer.start();

  std::vector<PageResult> results(options.pages);
  std::atomic<int> nextPage(0);
  std::mutex outputMutex;
  int done = 0;
  std::vector<std::thread> workers;
  for (int j = 0; j < options.jobs; ++j) {
    workers.emplace_back([&]() {
      for (int i = nextPage++; i < options.pages; i = nextPage++) {
        results[i] = measure(pageSpec(i, options.seed), options);
        const std::lock_guard<std::mutex> lock(outputMutex);
        ++done;
        std::cerr << "\rpage " << done << "/" << options.pages << std::flush;
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }
  std::cerr << "\rmeasured " << options.pages << " pages in " << timer.elapsed() / 1000 << " s" << std::endl;

  const std::vector<CaseImage> cases(caseImages(results, options));

  RunInfo info;
  info.pages = options.pages;
  info.seed = options.seed;
  info.fontFamily = QFontInfo(textFont(false)).family() + QStringLiteral(" / ") + QFontInfo(textFont(true)).family();
  info.seconds = timer.elapsed() / 1000.0;
  info.shift = SHIFT;
  for (const Method& method : methods()) {
    info.methods.push_back(method.name);
  }
  QString error;
  if (!writeReport(options.outDir, results, cases, info, &error)) {
    std::cerr << qPrintable(error) << std::endl;
    return 1;
  }
  std::cerr << "done in " << timer.elapsed() / 1000 << " s: " << qPrintable(QDir(options.outDir).absolutePath())
            << "/report.html" << std::endl;
  return 0;
}
