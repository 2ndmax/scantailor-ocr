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

#include <BackgroundColorCalculator.h>
#include <BinaryImage.h>
#include <ChromaDenoiser.h>
#include <ColorKMeans.h>
#include <ColorSegmenter.h>
#include <ConnectivityMap.h>
#include <CylindricalSurfaceDewarper.h>
#include <Despeckle.h>
#include <DistortionModel.h>
#include <DistortionModelBuilder.h>
#include <Dpi.h>
#include <Dpm.h>
#include <EstimateBackground.h>
#include <FilterData.h>
#include <Grayscale.h>
#include <ImageTransformation.h>
#include <KnnDenoiser.h>
#include <NullTaskStatus.h>
#include <OrthogonalRotation.h>
#include <PolynomialSurface.h>
#include <Posterizer.h>
#include <RasterDewarper.h>
#include <Sharpen.h>
#include <SkewFinder.h>
#include <SubtractBackground.h>
#include <TextLineTracer.h>
#include <TopBottomEdgeTracer.h>
#include <WienerFilter.h>
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
#include "filters/output/ColorCommonOptions.h"
#include "filters/output/DepthPerception.h"
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

// 5: automatic dewarping, as OutputGenerator builds and applies the distortion model: the text
// lines and the top and bottom edges of the content, the model, the dewarped page.
void autoDewarping(const Input& in) {
  const uint8_t* s = in.settings;
  const NullTaskStatus status;
  const GrayImage gray(in.image);
  QRect contentRect = gray.rect();
  if (s[0] & 1) {
    // A content box anywhere in the page, also an empty one.
    contentRect = QRect(s[1] * gray.width() / 256, s[2] * gray.height() / 256, s[3] * gray.width() / 256,
                        s[4] * gray.height() / 256);
  }
  dewarping::DistortionModelBuilder modelBuilder(Vec2d(0, 1));
  dewarping::TextLineTracer::trace(gray, in.dpi, contentRect, modelBuilder, status);
  dewarping::TopBottomEdgeTracer::trace(gray, modelBuilder.verticalBounds(), modelBuilder, status);
  const dewarping::DistortionModel model = modelBuilder.tryBuildModel();
  if (!model.isValid()) {
    return;
  }
  const output::DepthPerception depth(QString::number(s8(s[5]) / 8.0));
  const dewarping::CylindricalSurfaceDewarper dewarper(model.topCurve().polyline(), model.bottomCurve().polyline(),
                                                       depth.value(), model.widthCorrection());
  const QRect modelDomain(model.modelDomain(dewarper, QTransform(), gray.rect()).toRect());
  if (!modelDomain.isEmpty()) {
    (void) dewarping::RasterDewarper::dewarp(in.image, in.image.size(), dewarper, modelDomain, Qt::white);
  }
}

// 6: the color processing of the Output step, with its settings read as a project file stores them.
void colorProcessing(const Input& in) {
  const uint8_t* s = in.settings;
  const NullTaskStatus status;
  QDomDocument doc;
  // The settings of the Color / Grayscale panel, shared by subgroups 0 and 1.
  output::ColorCommonOptions commonDefaults;
  QDomElement commonEl = commonDefaults.toXml(doc, QStringLiteral("color-common-options"));
  // The settings of color segmentation, shared by subgroups 1 and 2.
  output::BlackWhiteOptions bwDefaults;
  QDomElement bwEl = bwDefaults.toXml(doc, QStringLiteral("black-white-options"));
  QDomElement segmenterEl = bwEl.namedItem(QStringLiteral("color-segmenter-options")).toElement();
  const QImage& image = in.image;
  // As OutputGenerator decides whether the original has colors.
  const bool colorOriginal = !image.allGray();
  const BinaryImage text = output::binarizeBlackWhite(image, bwDefaults);

  switch (s[0] % 4) {
    case 0: {
      // The enhancement filters, in the order of OutputGenerator::Processor::applyEnhancementFilters().
      commonEl.setAttribute(QStringLiteral("chromaDenoise"), 1);
      commonEl.setAttribute(QStringLiteral("chromaRadius"), s8(s[1]));
      commonEl.setAttribute(QStringLiteral("chromaGrayLimit"), s8(s[2]));
      commonEl.setAttribute(QStringLiteral("subtractBgRadius"), s8(s[1]));
      commonEl.setAttribute(QStringLiteral("subtractBgCoef"), s[3] / 200.0);
      commonEl.setAttribute(QStringLiteral("subtractBgWhite"), (s[0] & 4) ? 1 : 0);
      commonEl.setAttribute(QStringLiteral("knnRadius"), s8(s[4]));
      commonEl.setAttribute(QStringLiteral("knnCoef"), s[5] / 200.0);
      commonEl.setAttribute(QStringLiteral("wienerWinSize"), s8(s[6]));
      commonEl.setAttribute(QStringLiteral("wienerCoef"), s[5] / 200.0);
      commonEl.setAttribute(QStringLiteral("sharpenRadius"), s8(s[4]));
      commonEl.setAttribute(QStringLiteral("sharpenCoef"), s[7] / 80.0);
      const output::ColorCommonOptions options(commonEl);
      QImage enhanced(image);
      if (options.chromaDenoise()) {
        chromaDenoiseInPlace(enhanced, options.chromaRadius(), options.chromaGrayLimit());
      }
      subtractBackgroundColorInPlace(enhanced, options.subtractBackgroundRadius(), options.subtractBackgroundCoef(),
                                     options.subtractBackgroundWhite());
      knnDenoiseColorInPlace(enhanced, options.knnRadius(), options.knnCoef());
      wienerColorFilterInPlace(enhanced, QSize(options.wienerWindowSize(), options.wienerWindowSize()),
                               options.wienerCoef());
      sharpenColorInPlace(enhanced, options.sharpenRadius(), options.sharpenCoef());
      break;
    }
    case 1: {
      // Color segmentation of the text, then posterizing, as OutputGenerator::Processor::colorText()
      // does without K-means.
      segmenterEl.setAttribute(QStringLiteral("noiseReduction"), s8(s[1]));
      segmenterEl.setAttribute(QStringLiteral("redThresholdAdjustment"), s8(s[2]));
      segmenterEl.setAttribute(QStringLiteral("greenThresholdAdjustment"), s8(s[3]));
      segmenterEl.setAttribute(QStringLiteral("blueThresholdAdjustment"), s8(s[4]));
      QDomElement posterizationEl = commonEl.namedItem(QStringLiteral("posterization-options")).toElement();
      posterizationEl.setAttribute(QStringLiteral("level"), s8(s[5]));
      posterizationEl.setAttribute(QStringLiteral("normalizationEnabled"), (s[6] & 1) ? 1 : 0);
      posterizationEl.setAttribute(QStringLiteral("forceBlackAndWhite"), (s[6] & 2) ? 1 : 0);
      const output::BlackWhiteOptions::ColorSegmenterOptions segmenterOptions(segmenterEl);
      const ColorSegmenter segmenter(
          in.dpi, segmenterOptions.getNoiseReduction(), segmenterOptions.getRedThresholdAdjustment(),
          segmenterOptions.getGreenThresholdAdjustment(), segmenterOptions.getBlueThresholdAdjustment());
      const QImage segmented
          = colorOriginal ? segmenter.segment(text, image) : segmenter.segment(text, GrayImage(image)).toQImage();
      const output::ColorCommonOptions::PosterizationOptions posterization(posterizationEl);
      const QColor background = BackgroundColorCalculator::calcDominantBackgroundColor(image);
      const Posterizer posterizer(posterization.getLevel(), posterization.isNormalizationEnabled(),
                                  posterization.isForceBlackAndWhite(), 0, qRound(background.lightnessF() * 255));
      (void) posterizer.posterize(segmented);
      break;
    }
    case 2: {
      // K-means colors for the text, as OutputGenerator::Processor::colorText() does with K-means.
      segmenterEl.setAttribute(QStringLiteral("noiseReduction"), s8(s[1]));
      segmenterEl.setAttribute(QStringLiteral("kmeans"), 1);
      segmenterEl.setAttribute(QStringLiteral("kmeansColors"), s8(s[5]));
      segmenterEl.setAttribute(QStringLiteral("kmeansBlackFixed"), (s[6] & 1) ? 1 : 0);
      const output::BlackWhiteOptions::ColorSegmenterOptions options(segmenterEl);
      const ConnectivityMap segments
          = !colorOriginal ? ConnectivityMap(text, CONN8)
                           : ColorSegmenter(in.dpi, options.getNoiseReduction(), options.getRedThresholdAdjustment(),
                                            options.getGreenThresholdAdjustment(), options.getBlueThresholdAdjustment())
                                 .segmentMap(text, image);
      const int quarterInch = (in.dpi.horizontal() + in.dpi.vertical()) / 8;
      (void) Posterizer::convertToIndexed(reduceTextColorsKMeans(
          image, text, segments, options.getKMeansColors(), options.isKMeansBlackFixed(), quarterInch * quarterInch));
      break;
    }
    default: {
      // The background estimation of illumination normalization, in an area anywhere in the page.
      const QRectF area(s[1] * image.width() / 256.0, s[2] * image.height() / 256.0, s[3] * image.width() / 256.0,
                        s[4] * image.height() / 256.0);
      const GrayImage gray(image);
      const PolynomialSurface background = estimateBackground(gray, QPolygonF(area), status);
      (void) background.render(gray.size());
      (void) BackgroundColorCalculator::calcDominantBackgroundColor(image, QPolygonF(area));
      break;
    }
  }
}

// The order is the group number of the seeds; append new groups at the end.
void (*const groups[])(const Input&) = {
    binarization,      // 0
    despeckling,       // 1
    deskewing,         // 2
    pageSplitting,     // 3
    contentSelection,  // 4
    autoDewarping,     // 5
    colorProcessing,   // 6
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
