// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F5b: the Output step on a whole page, by OutputGenerator::process(). The input:
//
//   byte 0, 1    width and height minus 1 (1 to 128 pixels)
//   byte 2, 3    the DPI of the image (16 bit, big endian)
//   byte 4       bits 0-1: the orthogonal rotation, bit 2: color instead of gray, bit 3: the DPI
//                as they are instead of within 150 to 9999, bit 4: a page box inside the image,
//                bit 5: the illumination normalization as the XML says (else it is off)
//   byte 5      the deskew angle in 1/4 degree (signed)
//   byte 6..9    the content box: x, y, width, height in 1/256 of the deskewed page
//   byte 10      the margins around the content box in pixels (signed)
//   byte 11      the inset of the page box in 1/256 of the image (with bit 4 of byte 4)
//   byte 12, 13  the length of the XML that follows (big endian)
//   the XML      the output step's <page> element of a project file: <params>, <zones>,
//                <fill-zones>, <processing-params>
//   the rest     the pixels, 1 byte (gray) or 3 bytes (color) each; missing ones are white
//
// Seeds: corpus/page_output. Dictionary: page_output.dict.
//
// The page is read as output::Filter::loadSettings() reads it, the geometry is built as the
// steps before the Output step build it. Output resolutions above the 1200 DPI of the options
// panel are skipped: they make the output too large, which the program reports as intended.
// The illumination normalization is only kept with bit 5 of byte 4: it scales every page to about
// 300 pixels and runs several passes over it, which the instrumentation slows from milliseconds
// to seconds. Always on, as it is by default for black and white, it would leave a run a page
// every few seconds.
// Besides crashes and exceptions (other than std::bad_alloc), these count as findings, as the
// program relies on them: the output has the size the generator announces, black and white
// output has only black and white, and the layers of split output have the size of the page.

#include <BinaryImage.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <Grayscale.h>
#include <ImageId.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <OrthogonalRotation.h>
#include <PageId.h>

#include <QDomDocument>
#include <QImage>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>

#include "FuzzSetup.h"
#include "filters/output/FillZonePropFactory.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/OutputImageWithForeground.h"
#include "filters/output/OutputImageWithOriginalBackground.h"
#include "filters/output/OutputProcessingParams.h"
#include "filters/output/Params.h"
#include "filters/output/PictureZonePropFactory.h"
#include "filters/output/RenderParams.h"
#include "filters/output/Settings.h"
#include "filters/page_layout/Utils.h"
#include "zones/ZoneSet.h"

using namespace imageproc;

namespace {
const size_t HEADER_SIZE = 14;
const int MAX_OUTPUT_DPI = 1200;

// A broken rule: reported like a crash, so that libFuzzer keeps the input.
void check(const bool ok, const char* rule) {
  if (!ok) {
    std::fprintf(stderr, "Rule broken: %s\n", rule);
    std::abort();
  }
}

int read16(const uint8_t* p) {
  return (p[0] << 8) | p[1];
}

int s8(const uint8_t v) {
  return static_cast<int8_t>(v);
}

QImage buildImage(const uint8_t* header, const uint8_t* pixels, size_t left) {
  const int width = header[0] % 128 + 1;
  const int height = header[1] % 128 + 1;
  const bool color = (header[4] & 4) != 0;
  QImage image(width, height, color ? QImage::Format_RGB32 : QImage::Format_Grayscale8);
  for (int y = 0; y < height; ++y) {
    uchar* line = image.scanLine(y);
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
    image = toGrayscale(image);
  }
  int dpi = read16(header + 2);
  if (!(header[4] & 8)) {
    dpi = 150 + dpi % 9850;
  }
  const Dpm dpm(Dpi(dpi, dpi));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return image;
}

// A rectangle inside \p rect, given in 1/256 of its size.
QRectF fraction(const QRectF& rect, const uint8_t* f) {
  return QRectF(rect.x() + f[0] * rect.width() / 256, rect.y() + f[1] * rect.height() / 256, f[2] * rect.width() / 256,
                f[3] * rect.height() / 256);
}

bool onlyBlackAndWhite(const QImage& image) {
  const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  for (int y = 0; y < gray.height(); ++y) {
    const uchar* line = gray.constScanLine(y);
    for (int x = 0; x < gray.width(); ++x) {
      if ((line[x] != 0) && (line[x] != 255)) {
        return false;
      }
    }
  }
  return true;
}

void checkLayer(const QImage& layer, const QSize& size, const char* rule) {
  check(layer.isNull() || (layer.size() == size), rule);
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  if (size < HEADER_SIZE) {
    return 0;
  }
  const size_t xmlSize = std::min<size_t>(read16(data + 12), size - HEADER_SIZE);
  QDomDocument doc;
  if (!doc.setContent(fuzz::bytes(data + HEADER_SIZE, xmlSize))) {
    return 0;
  }

  // The page as output::Filter::loadSettings() reads it.
  const QDomElement pageEl = doc.documentElement();
  output::Params params(pageEl.namedItem("params").toElement());
  if (!(data[4] & 32)) {
    output::ColorParams colorParams = params.colorParams();
    output::BlackWhiteOptions blackWhiteOptions = colorParams.blackWhiteOptions();
    blackWhiteOptions.setNormalizeIllumination(false);
    colorParams.setBlackWhiteOptions(blackWhiteOptions);
    output::ColorCommonOptions colorCommonOptions = colorParams.colorCommonOptions();
    colorCommonOptions.setNormalizeIllumination(false);
    colorParams.setColorCommonOptions(colorCommonOptions);
    params.setColorParams(colorParams);
  }
  if ((params.outputDpi().horizontal() > MAX_OUTPUT_DPI) || (params.outputDpi().vertical() > MAX_OUTPUT_DPI)) {
    return 0;
  }
  ZoneSet pictureZones(pageEl.namedItem("zones").toElement(), output::PictureZonePropFactory());
  const ZoneSet fillZones(pageEl.namedItem("fill-zones").toElement(), output::FillZonePropFactory());
  const PageId pageId(ImageId(QStringLiteral("page.png")));
  auto settings = std::make_shared<output::Settings>();
  settings->setParams(pageId, params);
  const QDomElement processingEl = pageEl.namedItem("processing-params").toElement();
  if (!processingEl.isNull()) {
    settings->setOutputProcessingParams(pageId, output::OutputProcessingParams(processingEl));
  }

  // The geometry as the steps before build it: Split Pages the page box, Deskew the angle,
  // Margins the content box with its margins.
  const FilterData loaded(buildImage(data, data + HEADER_SIZE + xmlSize, size - HEADER_SIZE - xmlSize));
  ImageTransformation xform(loaded.xform());
  OrthogonalRotation rotation;
  for (int i = 0; i < (data[4] & 3); ++i) {
    rotation.nextClockwiseDirection();
  }
  xform.setPreRotation(rotation);
  if (data[4] & 16) {
    const QRectF imageRect(loaded.origImage().rect());
    const double inset = data[11] * std::min(imageRect.width(), imageRect.height()) / 512;
    xform.setPreCropArea(xform.preRotation()
                             .transform(imageRect.size())
                             .map(QPolygonF(imageRect.adjusted(inset, inset, -inset, -inset))));
  }
  xform.setPostRotation(s8(data[5]) / 4.0);
  const QRectF contentRect = fraction(xform.resultingRect(), data + 6);
  const QPolygonF contentRectPhys = xform.transformBack().map(contentRect);
  const int margin = s8(data[10]);
  const QPolygonF pageRectPhys = xform.transformBack().map(contentRect.adjusted(-margin, -margin, margin, margin));
  xform.setPostCropArea(page_layout::Utils::shiftToRoundedOrigin(xform.transform().map(pageRectPhys)));
  const FilterData pageData(loaded, xform);

  // The Output step itself.
  ImageTransformation outputXform(xform);
  outputXform.postScaleToDpi(params.outputDpi());
  const output::OutputGenerator generator(outputXform, contentRectPhys);
  dewarping::DistortionModel distortionModel;
  if (params.dewarpingOptions().dewarpingMode() == output::MANUAL) {
    distortionModel = params.distortionModel();
  }
  BinaryImage autoPictureMask;
  BinaryImage specklesImage;
  std::unique_ptr<output::OutputImage> outputImage;
  try {
    outputImage = generator.process(NullTaskStatus(), pageData, pictureZones, fillZones, distortionModel,
                                    params.depthPerception(), &autoPictureMask, &specklesImage, nullptr, pageId,
                                    settings, true);
  } catch (const std::bad_alloc&) {
    // The program hands it to its out-of-memory handler.
    return 0;
  }

  check(outputImage && !outputImage->isNull(), "the output is not empty");
  const QSize outputSize = generator.outputImageSize();
  const QImage image = outputImage->toImage();
  check(image.size() == outputSize, "the output has the size the generator announces");
  const output::RenderParams renderParams(params.colorParams(), params.splittingOptions());
  if (renderParams.binaryOutput() && !renderParams.needColorSegmentation() && !renderParams.splitOutput()) {
    check(onlyBlackAndWhite(image), "black and white output has only black and white");
  }
  if (const auto* layered = dynamic_cast<const output::OutputImageWithForeground*>(outputImage.get())) {
    checkLayer(layered->getForegroundImage(), outputSize, "the foreground layer has the size of the page");
    checkLayer(layered->getBackgroundImage(), outputSize, "the background layer has the size of the page");
  }
  if (const auto* original = dynamic_cast<const output::OutputImageWithOriginalBackground*>(outputImage.get())) {
    checkLayer(original->getOriginalBackgroundImage(), outputSize,
               "the original background layer has the size of the page");
  }
  return 0;
}
