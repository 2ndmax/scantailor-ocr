// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: measures the automatic oblique (shear) detection on synthetic test pages with a
// known angle. Each page is measured twice, the second time moved by a fraction of a pixel, which
// shows how stable the measurement is. The methods compared are listed in METHODS; the first is
// the one the program uses.
//
// Usage: oblique_compare <output folder> [--pages=<n>] [--seed=<n>] [--jobs=<n>]
//
// The pages come from a fixed pseudo-random sequence (see TestPage.h): the same seed gives the
// same pages. The output folder gets bericht.html (German, with images of the most notable cases
// in bilder/), ergebnisse.csv (one row per page and method) and zusammenfassung.txt (the tables
// and the conclusions).

#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <Dpi.h>
#include <NullTaskStatus.h>
#include <SkewFinder.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFontInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

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

/** An oblique method: the angle in the sense of deskew::Task::findObliqueAngle(), or nothing. */
struct Method {
  QString name;
  std::function<std::optional<double>(SkewFinder&, const BinaryImage& cleaned, double deskewAngleDeg)> find;
};

const std::vector<Method>& methods() {
  static const std::vector<Method> METHODS
      = {{QStringLiteral("heute"), [](SkewFinder& skewFinder, const BinaryImage& image, const double deskewAngleDeg) {
            return deskew::Task::findObliqueAngle(skewFinder, image, deskewAngleDeg);
          }}};
  return METHODS;
}

struct Options {
  QString outDir;
  int pages = 2 * PAGES_PER_ROUND;
  uint32_t seed = 20261026;
  int jobs = 1;
};

/** The black-and-white page the deskew step measures on, cleaned up like there. */
BinaryImage cleanedPage(const QImage& scan, const int dpi) {
  BinaryImage image(scan, BinaryThreshold::otsuThreshold(scan));
  deskew::Task::cleanup(NullTaskStatus(), image, Dpi(dpi, dpi));
  return image;
}

/**
 * Like the program: the deskew angle first, then each oblique method on the page turned by it.
 * \p shifted selects which of the two results of each method is filled.
 */
void measureScan(const QImage& scan, const int dpi, const bool shifted, PageResult& result) {
  const BinaryImage image(cleanedPage(scan, dpi));
  SkewFinder skewFinder;
  const std::optional<double> rotation(deskew::Task::findDeskewAngle(skewFinder, image, deskew::DETECT_CONTENT));
  if (!shifted) {
    result.rotation = rotation;
  }
  for (size_t m = 0; m < methods().size(); ++m) {
    const std::optional<double> shear(methods()[m].find(skewFinder, image, rotation.value_or(0.0)));
    (shifted ? result.methods[m].shearShifted : result.methods[m].shear) = shear;
  }
}

PageResult measure(const PageSpec& spec, const Options& options, const QFont& font) {
  const QImage upright(renderPage(spec, options.seed, font));
  PageResult result;
  result.spec = spec;
  result.methods.resize(methods().size());
  measureScan(scannedPage(upright, spec, options.seed), spec.dpi, false, result);
  measureScan(scannedPage(upright, spec, options.seed, SHIFT), spec.dpi, true, result);
  return result;
}

QString number(const double value, const int decimals = 2) {
  return QString::number(value, 'f', decimals).replace(QLatin1Char('.'), QLatin1Char(','));
}

QString angleText(const std::optional<double>& angle) {
  return angle ? number(*angle) + QStringLiteral("°") : QStringLiteral("kein Winkel");
}

QString pageText(const PageSpec& s) {
  return QStringLiteral("Seite %1 · %2 dpi · %3 · %4 · %5 · Störung: %6")
      .arg(s.index)
      .arg(s.dpi)
      .arg(toString(s.text), toString(s.layout), toString(s.alignment), toString(s.disturbance));
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

std::vector<CaseImage> caseImages(const std::vector<PageResult>& results, const Options& options, const QFont& font) {
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
      const QImage upright(renderPage(r->spec, options.seed, font));
      const double rotation = r->rotation.value_or(0.0);
      const QImage image(angleImage(scannedPage(upright, r->spec, options.seed),
                                    vertical(r->spec.rotationDeg, r->spec.shearDeg),
                                    vertical(rotation, r->methods[m].shear.value_or(0.0))));
      const QString file(QStringLiteral("bilder/m%1_%2.png").arg(m).arg(r->spec.index, 4, 10, QLatin1Char('0')));
      if (!image.save(options.outDir + QLatin1Char('/') + file)) {
        std::cerr << "Cannot write " << qPrintable(file) << std::endl;
        continue;
      }
      const QString caption(pageText(r->spec)
                            + QStringLiteral(": Scherung wahr %1, erkannt %2, verschoben %3 (Drehung wahr %4, "
                                             "erkannt %5)")
                                  .arg(angleText(r->spec.shearDeg), angleText(r->methods[m].shear),
                                       angleText(r->methods[m].shearShifted), angleText(r->spec.rotationDeg),
                                       angleText(r->rotation)));
      cases.push_back({method, file, caption});
    }
  }
  return cases;
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
                 "Defaults: 162 pages (every combination twice), seed 20261026, half of the processor cores "
                 "(at most 6)."
              << std::endl;
    return 1;
  }
  if (!QDir().mkpath(options.outDir + QStringLiteral("/bilder"))) {
    std::cerr << "Cannot create " << qPrintable(options.outDir) << std::endl;
    return 1;
  }
  // Images of an earlier run would stay next to the new ones.
  QDir images(options.outDir + QStringLiteral("/bilder"));
  for (const QString& file : images.entryList({QStringLiteral("m*_*.png")})) {
    images.remove(file);
  }

  const QFont font(textFont());
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
        results[i] = measure(pageSpec(i, options.seed), options, font);
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

  const std::vector<CaseImage> cases(caseImages(results, options, font));

  RunInfo info;
  info.pages = options.pages;
  info.seed = options.seed;
  info.fontFamily = QFontInfo(font).family();
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
            << "/bericht.html" << std::endl;
  return 0;
}
