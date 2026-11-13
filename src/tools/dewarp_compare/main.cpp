// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Developer tool: measures the automatic dewarping methods of the output step (Auto and Trapezoid) on drawn
// pages with a known distortion.
//
// Usage: dewarp_compare <output folder> [--seeds=<n>] [--jobs=<n>] [--only=<part of a case name>]
//        dewarp_compare <output folder> --images=<folder>
//
// With --images the distortion of a page is unknown, so the tool measures the traced text lines instead: how far
// their ends are off the straight middle part of the line, on the page as it is and where each method puts them
// (stdout and real-pages.txt, with the time each method takes). The methods are Auto, Trapezoid and "curved", the
// curved path of Auto taken also with a page Auto holds for flat. For every image of the folder and each method,
// the tool writes the page with the model drawn on it, the dewarped page, and the steps of the tracing that the
// debug mode of the program shows (traced lines, edges, the model among the lines).
//
// A case is a layout (justified, ragged right, ...) with a distortion (none, shear, converging lines, ...). Each case
// is drawn --seeds times with other random line lengths and positions. The place of every text line is known on
// the flat and on the distorted page, so the tool can tell how flat a model makes the page, without tracing the
// result again:
//
//   lines:   how far a point of a text line is from the level of its line (px, largest on the page).
//            This shows wavy, bent and tilted lines.
//   columns: how far a point is from where an even horizontal scale puts it (px, largest on the page).
//            This shows a wrong trapezoid: text stretched at the top and squeezed at the bottom, tilted letters.
//
// The curved distortion only moves points up and down, as no real book does. So for it "columns" says little:
// the cylinder model takes a bent line for depth and stretches it.
//
// The summary (stdout and summary.txt) has one row per case and method: pages without a model, both measures
// before -> after (median of the pages with a model) and their worst page, and the pages a model made worse by
// more than half a pixel. For each row the worst page is written as an image with the model drawn on it.
// "Even out letter widths" is not part of the measurement.

#include <AutoRemovingFile.h>
#include <BinaryImage.h>
#include <Constants.h>
#include <CylindricalSurfaceDewarper.h>
#include <DebugImages.h>
#include <DistortionModel.h>
#include <DistortionModelBuilder.h>
#include <Dpi.h>
#include <GrayImage.h>
#include <ImageLoader.h>
#include <NullTaskStatus.h>
#include <RasterDewarper.h>
#include <TextLineTracer.h>
#include <TopBottomEdgeTracer.h>
#include <VecNT.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "filters/output/DepthPerception.h"
#include "tools/ToolImageDpi.h"
#include "tools/ToolImageSave.h"

namespace dewarping {
// TextLineTracer befriends this name for dewarping_tests. Here it lets the tool take the curved path of Auto
// with a page Auto holds for flat.
struct TextLineTracerTestAccess {
  static void traceAsCurved(const imageproc::GrayImage& input,
                            const Dpi& dpi,
                            const QRect& contentRect,
                            DistortionModelBuilder& output,
                            DebugImages* dbg) {
    TextLineTracer::traceAsCurved(input, dpi, contentRect, output, dbg);
  }
};
}  // namespace dewarping

using namespace dewarping;

namespace {
// A page at 200 DPI, the resolution the tracing works at.
const QSize kPageSize(1000, 1500);
const double kTextLeft = 100;
const double kTextWidth = 800;
const double kLineStep = 34;
const double kLetterWidth = 7;
const double kLetterHeight = 16;
const double kWorseBy = 0.5;  // A model that adds more than this many pixels of error made the page worse.

struct TextLine {
  double left;
  double right;
  double top;
};

enum class Layout { Justified, RaggedRight, Paragraphs, Poem, ShortBlock, ShortRagged };

const std::vector<std::pair<Layout, const char*>> kLayouts{
    {Layout::Justified, "justified"}, {Layout::RaggedRight, "ragged-right"}, {Layout::Paragraphs, "paragraphs"},
    {Layout::Poem, "poem"},           {Layout::ShortBlock, "short-block"},   {Layout::ShortRagged, "short-ragged"}};

struct Distortion {
  const char* name;
  double tiltDegrees;    // The whole page turned, as left over by the deskew step.
  double linesConverge;  // Per pixel: the lines meet far to the right of the page.
  double edgesConverge;  // Per pixel: the edges meet far below the page.
  double curveHeight;    // Pixels the left end of a line hangs down, as towards the spine of a book.
  double shearDegrees;   // The edges lean while the lines stay level, as left over by the oblique correction.
  double spineRise = 0;  // Pixels the left end of the lowest line rises over its last quarter; the rest is straight.
};

const std::vector<Distortion> kDistortions{
    {"none", 0, 0, 0, 0, 0},     {"tilt", 0.4, 0, 0, 0, 0},     {"shear", 0, 0, 0, 0, 0.4},
    {"lines", 0, 3e-5, 0, 0, 0}, {"edges", 0, 0, 2.5e-5, 0, 0}, {"lines+edges", 0, 3e-5, 2.5e-5, 0, 0},
    {"curved", 0, 0, 0, 25, 0},  {"spine", 0, 0, 0, 0, 0, 5},   {"lines+spine", 0, 3e-5, 0, 0, 0, 5}};

QPointF warped(const Distortion& distortion, const QPointF& flat) {
  const QPointF center(kPageSize.width() / 2.0, kPageSize.height() / 2.0);
  const double angle = distortion.tiltDegrees * constants::DEG2RAD;
  const QPointF offCenter(flat - center);
  QPointF turned(offCenter.x() * std::cos(angle) - offCenter.y() * std::sin(angle),
                 offCenter.x() * std::sin(angle) + offCenter.y() * std::cos(angle));
  turned.rx() += std::tan(distortion.shearDegrees * constants::DEG2RAD) * turned.y();
  turned /= 1.0 + distortion.linesConverge * turned.x() + distortion.edgesConverge * turned.y();

  const double fromLeft = std::clamp((flat.x() - kTextLeft) / kTextWidth, 0.0, 1.0);
  const double sag = distortion.curveHeight * std::pow(1.0 - fromLeft, 3) * (0.6 + 0.8 * flat.y() / kPageSize.height());
  // As measured on scans of a thick book: Auto takes the bend from all text lines together.
  const double nearSpine = std::max(0.0, 1.0 - fromLeft / 0.25);
  // The lines run together towards the spine: those low on the page rise, those at its top sink.
  const double rise = distortion.spineRise * nearSpine * nearSpine * (flat.y() / kPageSize.height() - 0.3) / 0.7;
  return center + turned + QPointF(0, sag - rise);
}

/** The text lines of a layout. Line lengths and the place of the block differ with the seed. */
std::vector<TextLine> textLinesOf(const Layout layout, const unsigned seed) {
  std::mt19937 rng(seed);
  const auto random = [&rng](const double from, const double to) {
    return from + (to - from) * (rng() / static_cast<double>(std::mt19937::max()));
  };

  const double left = kTextLeft + random(-20, 20);
  const double right = left + kTextWidth;
  const double firstTop = 110 + random(-15, 15);
  const bool isShort = (layout == Layout::ShortBlock) || (layout == Layout::ShortRagged);
  const int numLines = isShort ? 13 : 36;

  std::vector<TextLine> lines;
  int untilParagraphEnd = 4 + static_cast<int>(random(0, 6));
  for (int i = 0; i < numLines; ++i) {
    TextLine line{left, right, firstTop + i * kLineStep};
    switch (layout) {
      case Layout::Justified:
      case Layout::ShortBlock:
        break;
      case Layout::RaggedRight:
      case Layout::ShortRagged:
        line.right = left + kTextWidth * random(0.82, 1.0);
        break;
      case Layout::Poem:
        line.right = left + kTextWidth * random(0.4, 0.85);
        break;
      case Layout::Paragraphs:
        if (untilParagraphEnd == 0) {
          line.right = left + kTextWidth * random(0.3, 0.9);
          untilParagraphEnd = 4 + static_cast<int>(random(0, 6));
          if (i + 1 < numLines) {
            lines.push_back(line);
            line = TextLine{left + 30, right, firstTop + ++i * kLineStep};  // The indented line after it.
          }
        } else {
          --untilParagraphEnd;
        }
        break;
    }
    lines.push_back(line);
  }
  if (layout == Layout::Paragraphs) {
    const double middle = 0.5 * (left + right);
    lines.push_back(TextLine{middle - 15, middle + 15, firstTop + (numLines + 2) * kLineStep});  // The page number.
  }
  return lines;
}

struct Page {
  QImage image;
  // The same points along every text line, on the flat and on the distorted page.
  std::vector<std::vector<QPointF>> flatLines;
  std::vector<std::vector<QPointF>> warpedLines;
};

Page drawPage(const Layout layout, const Distortion& distortion, const unsigned seed) {
  Page page;
  page.image = QImage(kPageSize, QImage::Format_Grayscale8);
  page.image.fill(Qt::white);
  QPainter painter(&page.image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(Qt::black);

  const auto drawLetter = [&](const double x, const double top) {
    QPolygonF letter;
    for (const QPointF& corner : {QPointF(x, top), QPointF(x + kLetterWidth, top),
                                  QPointF(x + kLetterWidth, top + kLetterHeight), QPointF(x, top + kLetterHeight)}) {
      letter << warped(distortion, corner);
    }
    painter.drawPolygon(letter);
  };

  for (const TextLine& line : textLinesOf(layout, seed)) {
    // Letters grouped into words, the last one ending exactly at the right end of the line.
    int lettersInWord = 0;
    for (double x = line.left; x + kLetterWidth < line.right - kLetterWidth; x += 11) {
      drawLetter(x, line.top);
      if (++lettersInWord == 6) {
        x += 8;
        lettersInWord = 0;
      }
    }
    drawLetter(line.right - kLetterWidth, line.top);

    std::vector<QPointF> flat;
    std::vector<QPointF> warpedLine;
    for (double x = line.left; x <= line.right; x += 10) {
      flat.emplace_back(x, line.top + kLetterHeight / 2);
      warpedLine.push_back(warped(distortion, flat.back()));
    }
    page.flatLines.push_back(flat);
    page.warpedLines.push_back(warpedLine);
  }
  return page;
}

/**
 * Curved is the curved path of Auto, taken whether or not the lines of the page are bent.
 */
enum class Method { Auto, Trapezoid, Curved };

const char* nameOf(const Method method) {
  switch (method) {
    case Method::Auto:
      return "auto";
    case Method::Trapezoid:
      return "trapezoid";
    case Method::Curved:
      return "curved";
  }
  return "";
}

/**
 * The model as OutputGenerator builds it; invalid if the method refuses the page.
 * \p heldForFlat tells whether Auto took the page for a flat one.
 */
DistortionModel modelOf(const Method method,
                        const QImage& image,
                        const Dpi& dpi = Dpi(200, 200),
                        DebugImages* dbg = nullptr,
                        bool* heldForFlat = nullptr) {
  const imageproc::GrayImage gray(image);
  if (method == Method::Trapezoid) {
    return TextLineTracer::traceTrapezoid(gray, dpi, image.rect(), dbg);
  }
  const NullTaskStatus status;
  DistortionModelBuilder builder(Vec2d(0, 1));
  if (method == Method::Curved) {
    TextLineTracerTestAccess::traceAsCurved(gray, dpi, image.rect(), builder, dbg);
  } else {
    const std::optional<DistortionModel> flatPageModel
        = TextLineTracer::trace(gray, dpi, image.rect(), builder, status, dbg);
    if (heldForFlat) {
      *heldForFlat = flatPageModel.has_value();
    }
    if (flatPageModel) {
      return *flatPageModel;
    }
  }
  TopBottomEdgeTracer::trace(gray, builder.verticalBounds(), builder, status);
  const QImage background = gray.toQImage();
  return builder.tryBuildModel(dbg, &background);
}

/** Collects the images the tracing shows in the program's debug mode. */
class KeptDebugImages : public DebugImages {
 public:
  void add(const QImage& image, const QString& label, const std::function<QWidget*(const QImage&)>&) override {
    images.emplace_back(label, image);
  }

  void add(const imageproc::BinaryImage&, const QString&, const std::function<QWidget*(const QImage&)>&) override {}

  bool empty() const override { return images.empty(); }

  AutoRemovingFile retrieveNext(QString*, std::function<QWidget*(const QImage&)>*) override {
    return AutoRemovingFile();
  }

  std::vector<std::pair<QString, QImage>> images;
};

struct Errors {
  double lines = 0;
  double columns = 0;
};

/** Slope of the least squares line through (from, to), i.e. how many units of \p to one pixel of the page is. */
double scaleOf(const std::vector<double>& from, const std::vector<double>& to, double& offset) {
  double meanFrom = 0;
  double meanTo = 0;
  for (size_t i = 0; i < from.size(); ++i) {
    meanFrom += from[i];
    meanTo += to[i];
  }
  meanFrom /= static_cast<double>(from.size());
  meanTo /= static_cast<double>(from.size());
  double sumXX = 0;
  double sumXY = 0;
  for (size_t i = 0; i < from.size(); ++i) {
    sumXX += (from[i] - meanFrom) * (from[i] - meanFrom);
    sumXY += (from[i] - meanFrom) * (to[i] - meanTo);
  }
  const double scale = sumXY / sumXX;
  offset = meanTo - scale * meanFrom;
  return scale;
}

/** Both measures of the header comment, for the points of the flat page and where a method puts them. */
Errors errorsOf(const std::vector<std::vector<QPointF>>& flatLines, const std::vector<std::vector<QPointF>>& result) {
  std::vector<double> flatX;
  std::vector<double> flatY;
  std::vector<double> resultX;
  std::vector<double> resultY;
  for (size_t line = 0; line < flatLines.size(); ++line) {
    for (size_t i = 0; i < flatLines[line].size(); ++i) {
      flatX.push_back(flatLines[line][i].x());
      flatY.push_back(flatLines[line][i].y());
      resultX.push_back(result[line][i].x());
      resultY.push_back(result[line][i].y());
    }
  }
  double xOffset = 0;
  double yOffset = 0;
  const double xScale = scaleOf(flatX, resultX, xOffset);
  const double yScale = scaleOf(flatY, resultY, yOffset);

  Errors errors;
  for (size_t i = 0; i < flatX.size(); ++i) {
    errors.columns = std::max(errors.columns, std::fabs((resultX[i] - (xOffset + xScale * flatX[i])) / xScale));
  }
  for (const std::vector<QPointF>& line : result) {
    double level = 0;
    for (const QPointF& pt : line) {
      level += pt.y();
    }
    level /= static_cast<double>(line.size());
    for (const QPointF& pt : line) {
      errors.lines = std::max(errors.lines, std::fabs((pt.y() - level) / yScale));
    }
  }
  return errors;
}

struct Measured {
  bool hasModel = false;
  Errors before;
  Errors after;
  DistortionModel model;
};

Measured measure(const Page& page, const Method method) {
  Measured measured;
  measured.before = errorsOf(page.flatLines, page.warpedLines);
  measured.after = measured.before;
  try {
    measured.model = modelOf(method, page.image);
    if (!measured.model.isValid()) {
      return measured;
    }
    const CylindricalSurfaceDewarper dewarper(measured.model.topCurve().polyline(),
                                              measured.model.bottomCurve().polyline(),
                                              output::DepthPerception::defaultValue());
    std::vector<std::vector<QPointF>> dewarped;
    for (const std::vector<QPointF>& line : page.warpedLines) {
      dewarped.emplace_back();
      for (const QPointF& pt : line) {
        dewarped.back().push_back(dewarper.mapToDewarpedSpace(pt));
      }
    }
    measured.after = errorsOf(page.flatLines, dewarped);
    measured.hasModel = true;
  } catch (const std::runtime_error&) {
    // The dewarper could not use the model; the page counts as one without a model.
  }
  return measured;
}

QImage withModel(const QImage& page, const DistortionModel& model) {
  QImage image(page.convertToFormat(QImage::Format_RGB32));
  if (!model.isValid()) {
    return image;
  }
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(Qt::red, 2));
  const std::vector<QPointF>& top = model.topCurve().polyline();
  const std::vector<QPointF>& bottom = model.bottomCurve().polyline();
  painter.drawPolyline(top.data(), static_cast<int>(top.size()));
  painter.drawPolyline(bottom.data(), static_cast<int>(bottom.size()));
  painter.setPen(QPen(Qt::blue, 2));
  painter.drawLine(top.front(), bottom.front());
  painter.drawLine(top.back(), bottom.back());
  return image;
}

double medianOf(std::vector<double> values) {
  std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
  return values[values.size() / 2];
}

struct Options {
  QString outDir;
  int seeds = 10;
  int jobs = 1;
  QString only;
  /** With --images: real pages from this folder instead of drawn ones. */
  QString imagesDir;
};

QImage withModel(const QImage& page, const DistortionModel& model);

// The resolution the real pages are dewarped at for the timing, a usual one for black and white output.
const double kOutputDpi = 600;

class Stopwatch {
 public:
  /** Milliseconds since the last call, or since the stopwatch was made. */
  double restartMs() {
    const auto now = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(now - m_start).count();
    m_start = now;
    return ms;
  }

 private:
  std::chrono::steady_clock::time_point m_start = std::chrono::steady_clock::now();
};

/** How straight the text lines of a page are; medians of the lines, in pixels. */
struct LineShape {
  int numLines = 0;
  double bow = 0;       // Of the parabola that fits the whole line, as Auto tells a curved page by.
  double leftEnd = 0;   // How far the left end of a line is off the straight line through its middle part.
  double rightEnd = 0;  // The same for the right end.
  double worst = 0;     // The farthest point of a line from that straight line.
};

/** The coefficients a, b, c of the least squares fit y = a + b * t + c * t^2. */
std::array<double, 3> fitParabola(const std::vector<QPointF>& points) {
  double s[5] = {0, 0, 0, 0, 0};
  double r[3] = {0, 0, 0};
  for (const QPointF& pt : points) {
    double power = 1;
    for (int i = 0; i < 5; ++i) {
      s[i] += power;
      if (i < 3) {
        r[i] += power * pt.y();
      }
      power *= pt.x();
    }
  }
  const auto det = [](const double m[3][3]) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
           + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
  };
  const double normal[3][3] = {{s[0], s[1], s[2]}, {s[1], s[2], s[3]}, {s[2], s[3], s[4]}};
  const double whole = det(normal);
  std::array<double, 3> coefficients{0, 0, 0};
  if (std::fabs(whole) < 1e-12) {
    return coefficients;
  }
  for (int column = 0; column < 3; ++column) {
    double replaced[3][3];
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) {
        replaced[i][j] = (j == column) ? r[i] : normal[i][j];
      }
    }
    coefficients[column] = det(replaced) / whole;
  }
  return coefficients;
}

/** \p lines in any coordinates with x along the lines; \p toPixels makes pixels of their y. */
LineShape shapeOfLines(const std::vector<std::vector<QPointF>>& lines, const double toPixels) {
  std::vector<double> bows;
  std::vector<double> leftEnds;
  std::vector<double> rightEnds;
  std::vector<double> worsts;
  for (const std::vector<QPointF>& line : lines) {
    const auto [minIt, maxIt] = std::minmax_element(line.begin(), line.end(),
                                                    [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    const double length = maxIt->x() - minIt->x();
    if ((line.size() < 8) || (length <= 0)) {
      continue;
    }
    // t runs from -1 at the left end to 1 at the right end.
    std::vector<QPointF> whole;
    std::vector<double> middleT;
    std::vector<double> middleY;
    for (const QPointF& pt : line) {
      whole.emplace_back(2.0 * (pt.x() - minIt->x()) / length - 1.0, pt.y() * toPixels);
      if (std::fabs(whole.back().x()) <= 0.6) {
        middleT.push_back(whole.back().x());
        middleY.push_back(whole.back().y());
      }
    }
    if (middleT.size() < 3) {
      continue;
    }
    double offset = 0;
    const double slope = scaleOf(middleT, middleY, offset);

    double left = 0;
    double right = 0;
    int numLeft = 0;
    int numRight = 0;
    double worst = 0;
    for (const QPointF& pt : whole) {
      const double off = pt.y() - (offset + slope * pt.x());
      worst = std::max(worst, std::fabs(off));
      if (pt.x() <= -0.9) {
        left += off;
        ++numLeft;
      } else if (pt.x() >= 0.9) {
        right += off;
        ++numRight;
      }
    }
    bows.push_back(fitParabola(whole)[2]);
    leftEnds.push_back(std::fabs(left) / std::max(numLeft, 1));
    rightEnds.push_back(std::fabs(right) / std::max(numRight, 1));
    worsts.push_back(worst);
  }

  LineShape shape;
  shape.numLines = static_cast<int>(bows.size());
  if (shape.numLines > 0) {
    shape.bow = medianOf(bows);
    shape.leftEnd = medianOf(leftEnds);
    shape.rightEnd = medianOf(rightEnds);
    shape.worst = medianOf(worsts);
  }
  return shape;
}

/** The shape of the lines where \p dewarper puts them, in pixels of the page as it was. */
LineShape shapeAfter(const std::vector<std::vector<QPointF>>& lines,
                     const CylindricalSurfaceDewarper& dewarper,
                     const double to200Dpi) {
  std::vector<std::vector<QPointF>> dewarped;
  std::vector<double> yBefore;
  std::vector<double> yAfter;
  for (const std::vector<QPointF>& line : lines) {
    dewarped.emplace_back();
    for (const QPointF& pt : line) {
      // The place along the line stays the one on the page as it was: a model that stretches the page near one
      // edge would else move the points that count as the end of the line.
      dewarped.back().emplace_back(pt.x(), dewarper.mapToDewarpedSpace(pt).y());
      yBefore.push_back(pt.y());
      yAfter.push_back(dewarped.back().back().y());
    }
  }
  if (yBefore.size() < 2) {
    return LineShape();
  }
  double offset = 0;
  const double unitsPerPixel = scaleOf(yBefore, yAfter, offset);
  return shapeOfLines(dewarped, to200Dpi / unitsPerPixel);
}

QString shapeText(const LineShape& shape) {
  return QStringLiteral("%1 %2 %3")
      .arg(shape.leftEnd, 5, 'f', 1)
      .arg(shape.rightEnd, 5, 'f', 1)
      .arg(shape.worst, 4, 'f', 1);
}

/** The page as the output step dewarps it, \p scale times as large; null if the model has no place on it. */
QImage dewarpedPage(const QImage& page,
                    const DistortionModel& model,
                    const CylindricalSurfaceDewarper& dewarper,
                    const double scale) {
  const QRect outRect(QPoint(0, 0), (QSizeF(page.size()) * scale).toSize());
  const QRect modelDomain(model.modelDomain(dewarper, QTransform::fromScale(scale, scale), outRect).toRect());
  if (modelDomain.isEmpty()) {
    return QImage();
  }
  return RasterDewarper::dewarp(page, outRect.size(), dewarper, modelDomain, Qt::white);
}

/**
 * Real pages have no known distortion, so nothing is measured: for every image of the folder, the model of each
 * method drawn on the page, and the steps of the tracing that show where a model comes from.
 */
int showModelsOfRealPages(const Options& options) {
  const QStringList keptLabels{QStringLiteral("traced"), QStringLiteral("filtered2"),
                               QStringLiteral("vertBounds_from_lines"), QStringLiteral("trimmed_polylines"),
                               QStringLiteral("distortion_model")};
  const QDir dir(options.imagesDir);
  const QStringList names
      = dir.entryList({"*.tif", "*.tiff", "*.png", "*.jpg", "*.jpeg", "*.jp2", "*.jpf"}, QDir::Files, QDir::Name);
  if (names.isEmpty()) {
    std::cerr << "No images in " << options.imagesDir.toStdString() << std::endl;
    return 1;
  }
  QString report
      = QStringLiteral(
            "Per page: traced lines, their bow, then for the page as it is and after each method: how far the left "
            "end,\n"
            "the right end and the worst point of a line are off the straight middle part of the line (px at 200 dpi,\n"
            "median of the lines, up or down alike). Second row: milliseconds for tracing alone, and per method for "
            "the "
            "model\n"
            "(tracing included) and for dewarping the image at %1 dpi.\n\n")
            .arg(kOutputDpi, 0, 'f', 0);
  std::cout << report.toStdString();
  for (const QString& name : names) {
    const QImage loaded = ImageLoader::load(dir.filePath(name));
    if (loaded.isNull()) {
      std::cerr << "Cannot read " << name.toStdString() << std::endl;
      return 1;
    }
    const Dpi dpi = tools::dpiOrDefault(loaded);
    const QImage image = imageproc::GrayImage(loaded).toQImage();
    const double to200Dpi = 200.0 / dpi.vertical();

    Stopwatch watch;
    const std::vector<std::vector<QPointF>> lines
        = TextLineTracer::tracedLines(imageproc::GrayImage(image), dpi, image.rect());
    const double traceMs = watch.restartMs();
    const LineShape before = shapeOfLines(lines, to200Dpi);

    QString row = QStringLiteral("%1 %2 %3  %4")
                      .arg(name, -24)
                      .arg(before.numLines, 5)
                      .arg(before.bow, 6, 'f', 1)
                      .arg(shapeText(before));
    QString times = QStringLiteral("  ms: trace %1").arg(traceMs, 0, 'f', 0);
    for (const Method method : {Method::Auto, Method::Trapezoid, Method::Curved}) {
      const char* const methodName = nameOf(method);
      const QString prefix
          = QDir(options.outDir).filePath(QFileInfo(name).completeBaseName() + QLatin1Char('_') + methodName);
      // Timed without the debug images, then once more for them.
      DistortionModel model;
      bool heldForFlat = false;
      watch.restartMs();
      try {
        model = modelOf(method, image, dpi, nullptr, &heldForFlat);
      } catch (const std::runtime_error&) {
        // No model, as below.
      }
      const double modelMs = watch.restartMs();

      QString result = QStringLiteral("no model");
      double rasterMs = 0;
      if (model.isValid()) {
        try {
          const CylindricalSurfaceDewarper dewarper(model.topCurve().polyline(), model.bottomCurve().polyline(),
                                                    output::DepthPerception::defaultValue());
          result = shapeText(shapeAfter(lines, dewarper, to200Dpi));
          watch.restartMs();
          const QImage dewarped = dewarpedPage(image, model, dewarper, kOutputDpi / dpi.vertical());
          rasterMs = watch.restartMs();
          tools::saveImage(dewarped, prefix + QLatin1String("_dewarped.png"));
        } catch (const std::runtime_error&) {
          result = QStringLiteral("unusable model");
        }
      }
      row += QStringLiteral(" | %1%2 %3")
                 .arg(QLatin1String(methodName),
                      QLatin1String((method == Method::Auto) ? (heldForFlat ? " (flat)" : " (curved)") : ""), result);
      times += QStringLiteral(", %1 %2 + raster %3")
                   .arg(QLatin1String(methodName))
                   .arg(modelMs, 0, 'f', 0)
                   .arg(rasterMs, 0, 'f', 0);

      KeptDebugImages dbg;
      try {
        model = modelOf(method, image, dpi, &dbg);
      } catch (const std::runtime_error&) {
        model = DistortionModel();
      }
      tools::saveImage(withModel(image, model), prefix + QLatin1String("_model.png"));
      for (const auto& [label, debugImage] : dbg.images) {
        if (keptLabels.contains(label)) {
          tools::saveImage(debugImage, prefix + QLatin1Char('_') + label + QLatin1String(".png"));
        }
      }
    }
    report += row + QLatin1Char('\n') + times + QLatin1Char('\n');
    std::cout << row.toStdString() << "\n" << times.toStdString() << std::endl;
  }

  QFile reportFile(QDir(options.outDir).filePath("real-pages.txt"));
  if (!reportFile.open(QIODevice::WriteOnly | QIODevice::Text) || (reportFile.write(report.toUtf8()) < 0)) {
    std::cerr << "Cannot write " << reportFile.fileName().toStdString() << std::endl;
    return 1;
  }
  return tools::exitCodeOfSaves();
}

bool parseArgs(const QStringList& args, Options& options) {
  options.jobs = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) / 2, 1, 6);
  for (int i = 1; i < args.size(); ++i) {
    const QString& arg = args[i];
    bool ok = true;
    if (arg.startsWith(QLatin1String("--seeds="))) {
      options.seeds = arg.mid(8).toInt(&ok);
      ok = ok && options.seeds > 0;
    } else if (arg.startsWith(QLatin1String("--jobs="))) {
      options.jobs = arg.mid(7).toInt(&ok);
      ok = ok && options.jobs > 0;
    } else if (arg.startsWith(QLatin1String("--only="))) {
      options.only = arg.mid(7);
    } else if (arg.startsWith(QLatin1String("--images="))) {
      options.imagesDir = arg.mid(9);
    } else if (!arg.startsWith(QLatin1String("--")) && options.outDir.isEmpty()) {
      options.outDir = arg;
    } else {
      ok = false;
    }
    if (!ok) {
      return false;
    }
  }
  return !options.outDir.isEmpty();
}

struct Case {
  Layout layout;
  const Distortion* distortion;
  Method method;
  QString name;
};
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  Options options;
  if (!parseArgs(app.arguments(), options)) {
    std::cerr << "Usage: dewarp_compare <output folder> [--seeds=<n>] [--jobs=<n>] [--only=<part of a case name>]\n"
                 "Defaults: 10 seeds, half of the processor cores (at most 6)."
              << std::endl;
    return 2;
  }
  if (!QDir().mkpath(options.outDir)) {
    std::cerr << "Cannot create " << options.outDir.toStdString() << std::endl;
    return 1;
  }
  if (!options.imagesDir.isEmpty()) {
    return showModelsOfRealPages(options);
  }

  std::vector<Case> cases;
  for (const auto& [layout, layoutName] : kLayouts) {
    for (const Distortion& distortion : kDistortions) {
      for (const Method method : {Method::Auto, Method::Trapezoid}) {
        const QString name
            = QStringLiteral("%1_%2_%3")
                  .arg(QLatin1String(layoutName), QLatin1String(distortion.name), QLatin1String(nameOf(method)));
        if (name.contains(options.only)) {
          cases.push_back(Case{layout, &distortion, method, name});
        }
      }
    }
  }

  const auto numSeeds = static_cast<size_t>(options.seeds);
  std::vector<Measured> results(cases.size() * numSeeds);
  std::atomic<size_t> next{0};
  std::vector<std::thread> workers;
  for (int j = 0; j < options.jobs; ++j) {
    workers.emplace_back([&]() {
      for (size_t i = next++; i < results.size(); i = next++) {
        const Case& oneCase = cases[i / numSeeds];
        const auto seed = static_cast<unsigned>(i % numSeeds) + 1;
        results[i] = measure(drawPage(oneCase.layout, *oneCase.distortion, seed), oneCase.method);
      }
    });
  }
  for (std::thread& worker : workers) {
    worker.join();
  }

  QString summary
      = QStringLiteral("%1 %2 %3 %4 %5 %6\n")
            .arg(QStringLiteral("case"), -38)
            .arg(QStringLiteral("no model"), 8)
            .arg(QStringLiteral("lines before -> after"), 22)
            .arg(QStringLiteral("worst"), 6)
            .arg(QStringLiteral("columns before -> after"), 24)
            .arg(QStringLiteral("worst"), 6)
        + QStringLiteral("%1 (px; median and worst of the pages with a model)  made worse\n").arg(QString(), 38);
  for (size_t c = 0; c < cases.size(); ++c) {
    std::vector<double> linesBefore;
    std::vector<double> linesAfter;
    std::vector<double> columnsBefore;
    std::vector<double> columnsAfter;
    int madeWorse = 0;
    size_t worstSeed = 0;
    double worstError = -1;
    for (size_t s = 0; s < numSeeds; ++s) {
      const Measured& measured = results[c * numSeeds + s];
      if (!measured.hasModel) {
        continue;
      }
      linesBefore.push_back(measured.before.lines);
      linesAfter.push_back(measured.after.lines);
      columnsBefore.push_back(measured.before.columns);
      columnsAfter.push_back(measured.after.columns);
      const bool worse = (measured.after.lines > measured.before.lines + kWorseBy)
                         || (measured.after.columns > measured.before.columns + kWorseBy);
      madeWorse += worse ? 1 : 0;
      const double error = std::max(measured.after.lines, measured.after.columns);
      if (error > worstError) {
        worstError = error;
        worstSeed = s;
      }
    }

    const size_t numWithModel = linesAfter.size();
    summary += QStringLiteral("%1 %2")
                   .arg(cases[c].name, -38)
                   .arg(QStringLiteral("%1/%2").arg(numSeeds - numWithModel).arg(numSeeds), 8);
    if (numWithModel > 0) {
      const auto px = [](const double value) { return QString::number(value, 'f', 1); };
      summary += QStringLiteral(" %1 %2 %3 %4  %5/%6")
                     .arg(px(medianOf(linesBefore)) + " -> " + px(medianOf(linesAfter)), 22)
                     .arg(px(*std::max_element(linesAfter.begin(), linesAfter.end())), 6)
                     .arg(px(medianOf(columnsBefore)) + " -> " + px(medianOf(columnsAfter)), 24)
                     .arg(px(*std::max_element(columnsAfter.begin(), columnsAfter.end())), 6)
                     .arg(madeWorse)
                     .arg(numWithModel);

      const auto seed = static_cast<unsigned>(worstSeed) + 1;
      const Page page = drawPage(cases[c].layout, *cases[c].distortion, seed);
      tools::saveImage(withModel(page.image, results[c * numSeeds + worstSeed].model),
                       QDir(options.outDir).filePath(QStringLiteral("%1_seed%2.png").arg(cases[c].name).arg(seed)));
    }
    summary += QLatin1Char('\n');
  }

  std::cout << summary.toStdString() << std::flush;
  QFile file(QDir(options.outDir).filePath(QStringLiteral("summary.txt")));
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text) || (file.write(summary.toUtf8()) < 0)) {
    std::cerr << "Cannot write " << file.fileName().toStdString() << std::endl;
    return 1;
  }
  return tools::exitCodeOfSaves();
}
