// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F5a: the processing of a page by the filters, on a small image built from the input. The input:
//
//   byte 0       the group (see the table below)
//   byte 1, 2    width and height minus 1 (1 to 256 pixels)
//   byte 3..6    horizontal and vertical DPI (16 bit each, big endian)
//   byte 7       bits 0-1: the orthogonal rotation of the page, bit 2: color instead of gray,
//                bit 3: the DPI as they are instead of within the range of 150 to 9999 DPI that
//                the program accepts (a project file can hold any)
//   byte 8..15   the settings of the group
//   the rest     the pixels, 1 byte (gray) or 3 bytes (color) each; missing ones are white
//
// Seeds: corpus/processing, their file names start with the group number.
//
// Each group calls what the filter's task calls, with its settings read from XML where the
// project file stores them, so that only values a project file can hold arrive. std::bad_alloc
// is left to the out-of-memory handler in the program; every other exception reaching the end is
// a finding: the program survives it, but shows the page as failed with a technical message.

#include <BinaryImage.h>
#include <Despeckle.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <Grayscale.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <OrthogonalRotation.h>
#include <SkewFinder.h>
#include <imageproc/OrthogonalRotation.h>

#include <QDomDocument>
#include <QImage>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <new>

#include "FuzzSetup.h"
#include "filters/deskew/Task.h"
#include "filters/output/BlackWhiteBinarization.h"
#include "filters/output/BlackWhiteOptions.h"
#include "filters/page_split/PageLayout.h"
#include "filters/page_split/PageLayoutEstimator.h"
#include "filters/select_content/ContentBoxFinder.h"
#include "filters/select_content/DetectionOptions.h"
#include "filters/select_content/PageFinder.h"

using namespace imageproc;

namespace {
const size_t HEADER_SIZE = 16;

struct Input {
  QImage image;  // With the DPI set.
  Dpi dpi;
  OrthogonalRotation rotation;
  const uint8_t* settings = nullptr;  // HEADER_SIZE - 8 bytes.
};

int read16(const uint8_t* p) {
  return (p[0] << 8) | p[1];
}

// A signed byte.
int s8(const uint8_t v) {
  return static_cast<int8_t>(v);
}

Input readInput(const uint8_t* data, const size_t size) {
  Input in;
  const int width = data[1] + 1;
  const int height = data[2] + 1;
  int xDpi = read16(data + 3);
  int yDpi = read16(data + 5);
  const uint8_t flags = data[7];
  if (!(flags & 8)) {
    xDpi = 150 + xDpi % 9850;
    yDpi = 150 + yDpi % 9850;
  }
  in.dpi = Dpi(xDpi, yDpi);
  for (int i = 0; i < (flags & 3); ++i) {
    in.rotation.nextClockwiseDirection();
  }
  in.settings = data + 8;

  const bool color = (flags & 4) != 0;
  in.image = QImage(width, height, color ? QImage::Format_RGB32 : QImage::Format_Grayscale8);
  const uint8_t* pixels = data + HEADER_SIZE;
  size_t left = size - HEADER_SIZE;
  for (int y = 0; y < height; ++y) {
    uchar* line = in.image.scanLine(y);
    for (int x = 0; x < width; ++x) {
      if (color) {
        const uint8_t r = left > 0 ? pixels[0] : 255;
        const uint8_t g = left > 1 ? pixels[1] : 255;
        const uint8_t b = left > 2 ? pixels[2] : 255;
        const size_t used = std::min<size_t>(left, 3);
        pixels += used;
        left -= used;
        reinterpret_cast<QRgb*>(line)[x] = qRgb(r, g, b);
      } else {
        line[x] = left > 0 ? *pixels : 255;
        if (left > 0) {
          ++pixels;
          --left;
        }
      }
    }
  }
  if (!color) {
    // As LoadFileTask::convertToSupportedFormat() does: the filters only take its formats.
    in.image = toGrayscale(in.image);
  }
  const Dpm dpm(in.dpi);
  in.image.setDotsPerMeterX(dpm.horizontal());
  in.image.setDotsPerMeterY(dpm.vertical());
  return in;
}

// The page as the filters get it: with the orthogonal rotation, and with \p postRotationDeg.
FilterData filterData(const Input& in, const double postRotationDeg = 0.0) {
  const FilterData loaded(in.image);
  ImageTransformation xform(loaded.xform());
  xform.setPreRotation(in.rotation);
  if (postRotationDeg != 0.0) {
    xform.setPostRotation(postRotationDeg);
  }
  return FilterData(loaded, xform);
}

// 0: binarization of the Output step, all methods with the settings a project file can hold.
void binarization(const Input& in) {
  const uint8_t* s = in.settings;
  output::BlackWhiteOptions options;
  options.setBinarizationMethod(static_cast<output::BinarizationMethod>(s[0] % (output::T_MULTISCALE + 1)));
  QDomDocument doc;
  QDomElement el = options.toXml(doc, QStringLiteral("black-white-options"));
  el.setAttribute(QStringLiteral("windowSize"), static_cast<int16_t>(read16(s + 1)));
  el.setAttribute(QStringLiteral("sauvolaCoef"), s8(s[3]) / 50.0);
  el.setAttribute(QStringLiteral("thresholdAdj"), s8(s[4]));
  el.setAttribute(QStringLiteral("wolfLowerBound"), s[5]);
  el.setAttribute(QStringLiteral("wolfUpperBound"), s[6]);
  el.setAttribute(QStringLiteral("wolfCoef"), s8(s[7]) / 50.0);
  (void) output::binarizeBlackWhite(in.image, output::BlackWhiteOptions(el));
}

// 1: despeckling of a binarized page, at any level a project file can hold.
void despeckling(const Input& in) {
  const FilterData data(in.image);
  const BinaryImage bw(data.grayImage(), data.bwThreshold());
  const double level = s8(in.settings[0]) / 32.0;
  (void) Despeckle::despeckle(bw, in.dpi, level, NullTaskStatus());
}

// 2: the Deskew step: cleanup, the deskew angle (by content or top edge), the oblique angle.
void deskewing(const Input& in) {
  const NullTaskStatus status;
  const FilterData data(filterData(in));
  BinaryImage rotated(orthogonalRotation(BinaryImage(data.grayImageBlackOnWhite(), data.bwThresholdBlackOnWhite()),
                                         in.rotation.toDegrees()));
  const Dpm rotatedDpm(in.rotation.rotate(Dpm(data.origImage()).toSize()));
  deskew::Task::cleanup(status, rotated, Dpi(rotatedDpm));
  SkewFinder skewFinder;
  skewFinder.setResolutionRatio(static_cast<double>(rotatedDpm.horizontal()) / rotatedDpm.vertical());
  const deskew::SkewDetection detection = (in.settings[0] & 1) ? deskew::DETECT_TOP_EDGE : deskew::DETECT_CONTENT;
  const double angle = deskew::Task::findDeskewAngle(skewFinder, rotated, detection).value_or(0.0);
  (void) deskew::Task::findObliqueAngle(status, rotated, angle, skewFinder.resolutionRatio());
}

// 3: the Split Pages step: the page layout for each layout type and detection method.
void pageSplitting(const Input& in) {
  const FilterData data(filterData(in));
  const auto layoutType = static_cast<page_split::LayoutType>(in.settings[0] % 4);
  const auto method = static_cast<page_split::SplitDetectionMethod>(in.settings[1] % 2);
  (void) page_split::PageLayoutEstimator::estimatePageLayout(layoutType, method, data.grayImage(), data.xform(),
                                                             data.bwThreshold());
}

// 4: the Select Content step on a deskewed page: the page box, then the content box in it.
void contentSelection(const Input& in) {
  const uint8_t* s = in.settings;
  const NullTaskStatus status;
  const FilterData data(filterData(in, s8(s[7]) / 4.0));
  const bool fineTune = (s[0] & 1) != 0;
  const QSizeF box(s[1], s[2]);
  const double tolerance = s8(s[3]) / 100.0;
  QRectF pageRect = data.xform().resultingRect();
  if (s[0] & 2) {
    pageRect = select_content::PageFinder::findPageBox(status, data, fineTune, box, tolerance);
  }
  QDomDocument doc;
  QDomElement el = doc.createElement(QStringLiteral("detection-options"));
  el.setAttribute(QStringLiteral("fillFactor"), s8(s[4]));
  el.setAttribute(QStringLiteral("borderTolerance"), s8(s[5]));
  el.setAttribute(QStringLiteral("ignorePageBox"), (s[0] & 4) ? 1 : 0);
  (void) select_content::ContentBoxFinder::findContentBox(status, data, pageRect, select_content::DetectionOptions(el));
}

// The order is the group number of the seeds; append new groups at the end.
void (*const groups[])(const Input&) = {
    binarization,      // 0
    despeckling,       // 1
    deskewing,         // 2
    pageSplitting,     // 3
    contentSelection,  // 4
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  if (size < HEADER_SIZE) {
    return 0;
  }
  const Input in = readInput(data, size);
  try {
    groups[data[0] % std::size(groups)](in);
  } catch (const std::bad_alloc&) {
    // The program hands it to its out-of-memory handler.
  }
  return 0;
}
