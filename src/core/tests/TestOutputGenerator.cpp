// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The rules of the Output step, checked on a small synthetic page: a "text" half with black
// strokes and a "picture" half with a gradient. The tests check properties of the result, not
// bytes, so they hold on every platform.

#include <BinaryImage.h>
#include <DefaultParams.h>
#include <DistortionModel.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <ImageSizeLimit.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PropertySet.h>
#include <filters/output/BlackWhiteOptions.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DepthPerception.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/OutputGenerator.h>
#include <filters/output/OutputImage.h>
#include <filters/output/Params.h>
#include <filters/output/PictureLayerProperty.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/output/Settings.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>
#include <zones/ZoneSet.h>

#include <QDomDocument>
#include <QImage>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace Tests {
using namespace output;

namespace {
const int WIDTH = 400;
const int HEIGHT = 360;
const Dpi DPI(300, 300);
// The picture half, in source image coordinates, with some distance to the text half.
const QRect PICTURE_RECT(220, 20, 160, 260);
// Inside the text half, where only black strokes on white are.
const QRect TEXT_RECT(20, 20, 160, 260);
// Strokes in the stroke color of a run, below the text half.
const QRect COLORED_RECT(20, 300, 160, 40);

// Black strokes on the left half, a gradient on the right half; light gray elsewhere.
QImage syntheticPage(const bool colored, const QRgb strokeColor) {
  QImage image(WIDTH, HEIGHT, QImage::Format_RGB32);
  image.fill(qRgb(250, 250, 250));
  for (int y = TEXT_RECT.top(); y <= TEXT_RECT.bottom(); ++y) {
    if ((y / 6) % 3 != 0) {
      continue;  // Lines of strokes with white between them.
    }
    for (int x = TEXT_RECT.left(); x <= TEXT_RECT.right(); ++x) {
      if ((x / 5) % 2 == 0) {
        image.setPixel(x, y, qRgb(0, 0, 0));
      }
    }
  }
  for (int y = PICTURE_RECT.top(); y <= PICTURE_RECT.bottom(); ++y) {
    for (int x = PICTURE_RECT.left(); x <= PICTURE_RECT.right(); ++x) {
      const int v = 60 + (x - PICTURE_RECT.left()) * 140 / PICTURE_RECT.width();
      image.setPixel(x, y, colored ? qRgb(v, 255 - v, 128) : qRgb(v, v, v));
    }
  }
  for (int y = COLORED_RECT.top(); y <= COLORED_RECT.bottom(); ++y) {
    for (int x = COLORED_RECT.left(); x <= COLORED_RECT.right(); ++x) {
      if (((y / 6) % 3 == 0) && ((x / 5) % 2 == 0)) {
        image.setPixel(x, y, strokeColor);
      }
    }
  }
  const Dpm dpm(DPI);
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return image;
}

ZoneSet pictureZone() {
  PropertySet props;
  props.locateOrCreate<PictureLayerProperty>()->setLayer(PictureLayerProperty::ZONEPAINTER2);
  ZoneSet zones;
  zones.add(Zone(SerializableSpline(QPolygonF(QRectF(PICTURE_RECT))), props));
  return zones;
}

struct Run {
  ColorMode mode = BLACK_AND_WHITE;
  PhotoAdjustments adjustments;
  bool withPictureZone = false;
  bool colored = false;
  QRgb strokeColor = qRgb(0, 0, 0);
  double coloredDimming = 0.0;
  int lowerBound = 1;
  int upperBound = 254;
  // Color segmentation with K-means, the number of colors; 0 is off.
  int kmeansColors = 0;
  double postScaleFactor = 1.0;
  QRectF content = QRectF(0, 0, WIDTH, HEIGHT);
  // A manual distortion model that is valid, but less than half a pixel wide, so that
  // its model domain is empty.
  bool thinManualDewarping = false;
  BinarizationMethod binarization = T_OTSU;
  // Values as read from a project file (attribute values); empty keeps the defaults of the test.
  QString windowSizeAttribute;
  QString outputDpiAttribute;
  // Gets the size the generator announces for the output, if set.
  QSize* announcedSize = nullptr;
};

std::unique_ptr<OutputImage> generate(const Run& run) {
  const PageId pageId(ImageId(QStringLiteral("page.png")), PageId::SINGLE_PAGE);
  auto settings = std::make_shared<Settings>();
  Dpi outputDpi = DPI;
  if (!run.outputDpiAttribute.isEmpty()) {
    QDomDocument doc;
    QDomElement el = doc.createElement(QStringLiteral("dpi"));
    el.setAttribute(QStringLiteral("horizontal"), run.outputDpiAttribute);
    el.setAttribute(QStringLiteral("vertical"), run.outputDpiAttribute);
    outputDpi = Params::outputDpiFromXml(el);
  }
  Params params;
  params.setOutputDpi(outputDpi);
  ColorParams colorParams;
  colorParams.setColorMode(run.mode);
  colorParams.setPhotoAdjustments(run.adjustments);
  BlackWhiteOptions blackWhiteOptions = colorParams.blackWhiteOptions();
  blackWhiteOptions.setColoredDimming(run.coloredDimming);
  blackWhiteOptions.setWolfLowerBound(run.lowerBound);
  blackWhiteOptions.setWolfUpperBound(run.upperBound);
  if (run.kmeansColors > 0) {
    BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
    segmenterOptions.setEnabled(true);
    segmenterOptions.setKMeansEnabled(true);
    segmenterOptions.setKMeansColors(run.kmeansColors);
    blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  }
  blackWhiteOptions.setBinarizationMethod(run.binarization);
  if (!run.windowSizeAttribute.isEmpty()) {
    QDomDocument doc;
    QDomElement el = blackWhiteOptions.toXml(doc, QStringLiteral("black-white-options"));
    el.setAttribute(QStringLiteral("windowSize"), run.windowSizeAttribute);
    blackWhiteOptions = BlackWhiteOptions(el);
  }
  colorParams.setBlackWhiteOptions(blackWhiteOptions);
  params.setColorParams(colorParams);
  // Only the manual zone counts as a picture, so the results don't depend on picture detection.
  PictureShapeOptions shapeOptions;
  shapeOptions.setPictureShape(OFF_SHAPE);
  params.setPictureShapeOptions(shapeOptions);
  params.setDespeckleLevel(0.0);
  if (run.thinManualDewarping) {
    params.setDewarpingOptions(DewarpingOptions(MANUAL));
  }
  settings->setParams(pageId, params);

  const FilterData data(syntheticPage(run.colored, run.strokeColor));
  ImageTransformation xform(data.xform());
  xform.postScaleToDpi(outputDpi);
  if (run.postScaleFactor != 1.0) {
    xform.setPostScaleFactor(run.postScaleFactor);
  }
  const OutputGenerator generator(xform, QPolygonF(run.content));
  if (run.announcedSize) {
    *run.announcedSize = generator.outputImageSize();
  }

  ZoneSet pictureZones = run.withPictureZone ? pictureZone() : ZoneSet();
  const ZoneSet fillZones;
  dewarping::DistortionModel distortionModel;
  if (run.thinManualDewarping) {
    distortionModel.setTopCurve(dewarping::Curve(std::vector<QPointF>{QPointF(100, 100), QPointF(100.3, 100)}));
    distortionModel.setBottomCurve(dewarping::Curve(std::vector<QPointF>{QPointF(100, 200), QPointF(100.3, 200)}));
    BOOST_REQUIRE(distortionModel.isValid());
  }
  return generator.process(NullTaskStatus(), data, pictureZones, fillZones, distortionModel, DepthPerception(), nullptr,
                           nullptr, nullptr, pageId, settings, /*takeOverBlackOnWhite=*/true);
}

QImage process(const Run& run) {
  const std::unique_ptr<OutputImage> output = generate(run);
  BOOST_REQUIRE(output && !output->isNull());
  return output->toImage().convertToFormat(QImage::Format_RGB32);
}

std::set<int> grayLevels(const QImage& image, const QRect& rect) {
  std::set<int> levels;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      levels.insert(qGray(image.pixel(x, y)));
    }
  }
  return levels;
}

double meanGray(const QImage& image, const QRect& rect) {
  double sum = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      sum += qGray(image.pixel(x, y));
    }
  }
  return sum / (rect.width() * rect.height());
}

bool samePixels(const QImage& a, const QImage& b, const QRect& rect) {
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      if (a.pixel(x, y) != b.pixel(x, y)) {
        return false;
      }
    }
  }
  return true;
}

// The inner part of a rect, away from the edges where filters blend neighbors.
QRect inner(const QRect& rect) {
  return rect.adjusted(8, 8, -8, -8);
}

PhotoAdjustments brighter() {
  PhotoAdjustments adjustments;
  adjustments.setExposure(1.0);
  return adjustments;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OutputGeneratorTestSuite)

BOOST_AUTO_TEST_CASE(output_has_the_size_of_the_page) {
  const QImage output = process(Run());
  BOOST_CHECK_EQUAL(output.width(), WIDTH);
  BOOST_CHECK_EQUAL(output.height(), HEIGHT);
}

BOOST_AUTO_TEST_CASE(empty_dewarping_model_domain_gives_the_page_size) {
  // The content is smaller than the scan, so the output differs in size from it.
  Run run;
  run.content = QRectF(40, 30, 300, 250);
  const QSize pageSize = process(run).size();
  run.thinManualDewarping = true;
  for (const ColorMode mode : {BLACK_AND_WHITE, COLOR_GRAYSCALE, MIXED}) {
    run.mode = mode;
    run.withPictureZone = (mode == MIXED);
    const QImage output = process(run);
    BOOST_CHECK_EQUAL(output.width(), pageSize.width());
    BOOST_CHECK_EQUAL(output.height(), pageSize.height());
  }
}

BOOST_AUTO_TEST_CASE(black_and_white_output_is_only_black_and_white) {
  const QImage output = process(Run());
  const std::set<int> levels = grayLevels(output, output.rect());
  BOOST_CHECK(levels == (std::set<int>{0, 255}));
}

BOOST_AUTO_TEST_CASE(color_output_keeps_the_tones_of_pictures) {
  Run run;
  run.mode = COLOR_GRAYSCALE;
  const QImage output = process(run);
  BOOST_CHECK_GT(grayLevels(output, inner(PICTURE_RECT)).size(), 50u);
}

BOOST_AUTO_TEST_CASE(mixed_output_has_black_and_white_text_and_toned_pictures) {
  Run run;
  run.mode = MIXED;
  run.withPictureZone = true;
  const QImage output = process(run);
  const std::set<int> textLevels = grayLevels(output, inner(TEXT_RECT));
  BOOST_CHECK(textLevels == (std::set<int>{0, 255}));
  const std::set<int> pictureLevels = grayLevels(output, inner(PICTURE_RECT));
  BOOST_CHECK_GT(pictureLevels.size(), 50u);
  // Pure black and white are reserved for text, so pictures don't contain them.
  BOOST_CHECK_EQUAL(pictureLevels.count(0), 0u);
  BOOST_CHECK_EQUAL(pictureLevels.count(255), 0u);
}

BOOST_AUTO_TEST_CASE(photo_adjustments_change_only_pictures_in_mixed_output) {
  Run run;
  run.mode = MIXED;
  run.withPictureZone = true;
  const QImage plain = process(run);
  run.adjustments = brighter();
  const QImage adjusted = process(run);
  BOOST_CHECK(samePixels(plain, adjusted, inner(TEXT_RECT)));
  BOOST_CHECK_GT(meanGray(adjusted, inner(PICTURE_RECT)), meanGray(plain, inner(PICTURE_RECT)) + 20);
}

BOOST_AUTO_TEST_CASE(photo_adjustments_change_color_output) {
  Run run;
  run.mode = COLOR_GRAYSCALE;
  run.colored = true;
  const QImage plain = process(run);
  run.adjustments = brighter();
  const QImage adjusted = process(run);
  BOOST_CHECK_GT(meanGray(adjusted, inner(PICTURE_RECT)), meanGray(plain, inner(PICTURE_RECT)) + 20);
}

BOOST_AUTO_TEST_CASE(photo_adjustments_dont_change_black_and_white_output) {
  const QImage plain = process(Run());
  Run run;
  run.adjustments = brighter();
  const QImage adjusted = process(run);
  BOOST_CHECK(plain == adjusted);
}

BOOST_AUTO_TEST_CASE(post_scale_factor_scales_the_output) {
  Run run;
  run.postScaleFactor = 1.5;
  const QImage output = process(run);
  BOOST_CHECK_LE(std::abs(output.width() - WIDTH * 3 / 2), 1);
  BOOST_CHECK_LE(std::abs(output.height() - HEIGHT * 3 / 2), 1);
}

// A page that scales to less than half a pixel (e.g. a far too high DPI in the file header) still
// gives an image of at least 1 x 1 pixel, and the generator announces that size.
BOOST_AUTO_TEST_CASE(tiny_output_has_the_announced_size) {
  Run run;
  run.postScaleFactor = 0.001;  // 400 x 300 pixels scale to 0.4 x 0.3.
  QSize announced;
  run.announcedSize = &announced;
  const QImage output = process(run);
  BOOST_CHECK(!announced.isEmpty());
  BOOST_CHECK(output.size() == announced);
}

BOOST_AUTO_TEST_CASE(output_over_the_size_limit_is_refused) {
  // A thin strip, so that the output has few pixels in case it isn't refused.
  Run run;
  run.content = QRectF(0, 0, WIDTH, 2);
  run.postScaleFactor = (ImageSizeLimit::MAX_SIDE + 100.0) / WIDTH;
  try {
    generate(run);
    BOOST_ERROR("No exception for an output over the size limit");
  } catch (const std::runtime_error& e) {
    BOOST_CHECK(QString::fromUtf8(e.what()).contains(QString::number(ImageSizeLimit::MAX_SIDE)));
  }
}

// The options panel keeps the window size between 3 and 9999 pixels; a project file can hold any
// value. Every method that uses a window has to process the page with it.
BOOST_AUTO_TEST_CASE(window_size_out_of_range_in_project_file_is_processed) {
  for (const BinarizationMethod method : {T_SAUVOLA, T_WOLF, T_FOX, T_WINDOW, T_BRADLEY, T_GRAD, T_EDGEPLUS, T_BLURDIV,
                                          T_EDGEDIV, T_GATOS, T_NICK, T_SINGH, T_ROBUST, T_MULTISCALE}) {
    for (const char* windowSize : {"0", "-5", "2000000000"}) {
      BOOST_TEST_CONTEXT("binarization method " << static_cast<int>(method) << ", windowSize " << windowSize) {
        Run run;
        run.binarization = method;
        run.windowSizeAttribute = QString::fromLatin1(windowSize);
        try {
          const QImage output = process(run);
          BOOST_CHECK_EQUAL(output.width(), WIDTH);
          BOOST_CHECK_EQUAL(output.height(), HEIGHT);
        } catch (const std::exception& e) {
          BOOST_ERROR("Exception: " << e.what());
        }
      }
    }
  }
}

// The output resolution of a project file, outside of what the options panel allows: one below
// its minimum gives the default resolution, a huge one makes the output too large.
BOOST_AUTO_TEST_CASE(output_dpi_out_of_range_in_project_file) {
  Run defaultRun;
  defaultRun.outputDpiAttribute = QString::number(Params::DEFAULT_OUTPUT_DPI);
  const QSize defaultSize = process(defaultRun).size();
  for (const char* dpi : {"0", "-300", "1", "abc"}) {
    BOOST_TEST_CONTEXT("dpi " << dpi) {
      Run run;
      run.outputDpiAttribute = QString::fromLatin1(dpi);
      try {
        const QImage output = process(run);
        BOOST_CHECK_EQUAL(output.width(), defaultSize.width());
        BOOST_CHECK_EQUAL(output.height(), defaultSize.height());
      } catch (const std::exception& e) {
        BOOST_ERROR("Exception: " << e.what());
      }
    }
  }
  for (const char* dpi : {"100000", "2000000000"}) {
    BOOST_TEST_CONTEXT("dpi " << dpi) {
      Run run;
      run.outputDpiAttribute = QString::fromLatin1(dpi);
      try {
        generate(run);
        BOOST_ERROR("No exception for an output over the size limit");
      } catch (const std::runtime_error& e) {
        BOOST_CHECK(QString::fromUtf8(e.what()).contains(QString::number(ImageSizeLimit::MAX_SIDE)));
      }
    }
  }
}

// The edges of the fallbacks: the smallest value the options panel allows is kept, one below it
// gives the default. The output resolution of the default parameters is read the same way.
BOOST_AUTO_TEST_CASE(fallbacks_start_just_below_the_smallest_allowed_value) {
  QDomDocument doc;
  auto dpiElement = [&](const char* value) {
    QDomElement el = doc.createElement(QStringLiteral("dpi"));
    el.setAttribute(QStringLiteral("horizontal"), QString::fromLatin1(value));
    el.setAttribute(QStringLiteral("vertical"), QString::fromLatin1(value));
    return el;
  };
  BOOST_REQUIRE_EQUAL(Params::MIN_OUTPUT_DPI, 72);
  BOOST_CHECK(Params::outputDpiFromXml(dpiElement("72")) == Dpi(72, 72));
  BOOST_CHECK(Params::outputDpiFromXml(dpiElement("71"))
              == Dpi(Params::DEFAULT_OUTPUT_DPI, Params::DEFAULT_OUTPUT_DPI));

  for (const char* value : {"71", "0"}) {
    BOOST_TEST_CONTEXT("default parameters, dpi " << value) {
      QDomElement outputEl = doc.createElement(QStringLiteral("output-params"));
      outputEl.appendChild(dpiElement(value));
      const DefaultParams::OutputParams outputParams(outputEl);
      BOOST_CHECK(outputParams.getDpi() == Dpi(Params::DEFAULT_OUTPUT_DPI, Params::DEFAULT_OUTPUT_DPI));
    }
  }
  QDomElement outputEl = doc.createElement(QStringLiteral("output-params"));
  outputEl.appendChild(dpiElement("72"));
  BOOST_CHECK(DefaultParams::OutputParams(outputEl).getDpi() == Dpi(72, 72));

  BOOST_REQUIRE_EQUAL(BlackWhiteOptions::MIN_WINDOW_SIZE, 3);
  QDomElement optionsEl = BlackWhiteOptions().toXml(doc, QStringLiteral("black-white-options"));
  optionsEl.setAttribute(QStringLiteral("windowSize"), QStringLiteral("3"));
  BOOST_CHECK_EQUAL(BlackWhiteOptions(optionsEl).getWindowSize(), 3);
  optionsEl.setAttribute(QStringLiteral("windowSize"), QStringLiteral("2"));
  BOOST_CHECK_EQUAL(BlackWhiteOptions(optionsEl).getWindowSize(), BlackWhiteOptions::DEFAULT_WINDOW_SIZE);
}

// In these two tests the content box is the text half only; the picture half lies in the margins.

BOOST_AUTO_TEST_CASE(black_and_white_margins_are_filled_white) {
  Run run;
  run.content = QRectF(TEXT_RECT);
  const QImage output = process(run);
  BOOST_CHECK(grayLevels(output, inner(PICTURE_RECT)) == (std::set<int>{255}));
  BOOST_CHECK_GT(grayLevels(output, inner(TEXT_RECT)).count(0), 0u);
}

BOOST_AUTO_TEST_CASE(color_margins_are_filled_with_the_page_background) {
  Run run;
  run.mode = COLOR_GRAYSCALE;
  run.content = QRectF(TEXT_RECT);
  const QImage output = process(run);
  // One even tone, close to the light gray paper of the synthetic page; the gradient is gone.
  const std::set<int> marginLevels = grayLevels(output, inner(PICTURE_RECT));
  BOOST_REQUIRE_EQUAL(marginLevels.size(), 1u);
  BOOST_CHECK_GE(*marginLevels.begin(), 245);
  BOOST_CHECK_GT(grayLevels(output, inner(TEXT_RECT)).count(0), 0u);
}

// Light red strokes (gray about 190) come out white, dark red ones (gray about 60) black.
const QRgb LIGHT_RED = qRgb(255, 170, 170);
const QRgb DARK_RED = qRgb(200, 0, 0);

bool hasBlack(const QImage& image, const QRect& rect) {
  return grayLevels(image, rect).count(0) > 0;
}

int blackPixels(const QImage& image, const QRect& rect) {
  int count = 0;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    for (int x = rect.left(); x <= rect.right(); ++x) {
      count += (qGray(image.pixel(x, y)) == 0) ? 1 : 0;
    }
  }
  return count;
}

BOOST_AUTO_TEST_CASE(colored_strokes_without_dimming_follow_their_brightness) {
  Run run;
  run.strokeColor = LIGHT_RED;
  BOOST_CHECK(!hasBlack(process(run), inner(COLORED_RECT)));
  run.strokeColor = DARK_RED;
  BOOST_CHECK(hasBlack(process(run), inner(COLORED_RECT)));
}

BOOST_AUTO_TEST_CASE(positive_dimming_makes_colored_text_black) {
  Run run;
  run.strokeColor = LIGHT_RED;
  run.coloredDimming = 2.0;
  BOOST_CHECK(hasBlack(process(run), inner(COLORED_RECT)));
}

BOOST_AUTO_TEST_CASE(negative_dimming_removes_colored_marks) {
  Run run;
  run.strokeColor = DARK_RED;
  run.coloredDimming = -1.0;
  const QImage output = process(run);
  BOOST_CHECK(!hasBlack(output, inner(COLORED_RECT)));
  // Black text isn't colored, so it stays.
  BOOST_CHECK(hasBlack(output, inner(TEXT_RECT)));
}

BOOST_AUTO_TEST_CASE(lower_bound_makes_darker_pixels_black) {
  Run run;
  run.strokeColor = LIGHT_RED;
  run.lowerBound = 200;
  BOOST_CHECK(hasBlack(process(run), inner(COLORED_RECT)));
}

BOOST_AUTO_TEST_CASE(upper_bound_makes_lighter_pixels_white) {
  Run run;
  run.strokeColor = DARK_RED;
  const int before = blackPixels(process(run), inner(COLORED_RECT));
  run.upperBound = 40;
  const QImage output = process(run);
  // Savitzky-Golay smoothing darkens the edges of the strokes a little, so a thin outline stays.
  BOOST_CHECK_LT(blackPixels(output, inner(COLORED_RECT)), before / 3);
  BOOST_CHECK(hasBlack(output, inner(TEXT_RECT)));
}

BOOST_AUTO_TEST_CASE(default_bounds_change_nothing) {
  // 1 and 254 are left out for Otsu, so the result is as without any bounds.
  Run run;
  run.strokeColor = LIGHT_RED;
  const QImage withDefaults = process(run);
  run.lowerBound = 0;
  run.upperBound = 255;
  BOOST_CHECK(withDefaults == process(run));
}

BOOST_AUTO_TEST_CASE(kmeans_keeps_colored_text_in_few_colors) {
  Run run;
  run.strokeColor = DARK_RED;
  run.kmeansColors = 3;
  const QImage output = process(run);
  std::set<QRgb> colors;
  for (int y = 0; y < output.height(); ++y) {
    for (int x = 0; x < output.width(); ++x) {
      colors.insert(output.pixel(x, y) | 0xff000000u);
    }
  }
  // Three colors, a fixed black among them, plus the white paper.
  BOOST_CHECK_LE(colors.size(), 4u);
  BOOST_CHECK(colors.count(qRgb(0, 0, 0)) == 1);
  // The red strokes stay red, the black text black.
  bool red = false;
  for (int y = COLORED_RECT.top(); y <= COLORED_RECT.bottom(); ++y) {
    for (int x = COLORED_RECT.left(); x <= COLORED_RECT.right(); ++x) {
      const QRgb pixel = output.pixel(x, y);
      red = red || ((qRed(pixel) > 120) && (qGreen(pixel) < 80) && (qBlue(pixel) < 80));
    }
  }
  BOOST_CHECK(red);
  BOOST_CHECK(hasBlack(output, inner(TEXT_RECT)));
}

BOOST_AUTO_TEST_CASE(kmeans_settings_are_stored_only_when_set) {
  QDomDocument doc;
  BlackWhiteOptions::ColorSegmenterOptions options;
  QDomElement el = options.toXml(doc, "segmenter");
  BOOST_CHECK(!el.hasAttribute("kmeans") && !el.hasAttribute("kmeansColors") && !el.hasAttribute("kmeansBlackFixed"));

  options.setKMeansEnabled(true);
  options.setKMeansColors(8);
  options.setKMeansBlackFixed(false);
  el = options.toXml(doc, "segmenter");
  const BlackWhiteOptions::ColorSegmenterOptions read(el);
  BOOST_CHECK(read == options);
  BOOST_CHECK(read.isKMeansEnabled());
  BOOST_CHECK_EQUAL(read.getKMeansColors(), 8);
  BOOST_CHECK(!read.isKMeansBlackFixed());
}

BOOST_AUTO_TEST_CASE(older_projects_posterize_text_when_posterizing_was_on) {
  // Before, one setting posterized both whole color pages and the text colors.
  ColorParams params;
  ColorCommonOptions common = params.colorCommonOptions();
  ColorCommonOptions::PosterizationOptions posterization = common.getPosterizationOptions();
  posterization.setEnabled(true);
  common.setPosterizationOptions(posterization);
  params.setColorCommonOptions(common);
  QDomDocument doc;
  QDomElement el = params.toXml(doc, "color-params");
  el.namedItem("bw").toElement().namedItem("color-segmenter-options").toElement().removeAttribute("posterize");
  BOOST_CHECK(ColorParams(el).blackWhiteOptions().getColorSegmenterOptions().isPosterizeEnabled());

  // Projects since then keep the two apart.
  el = params.toXml(doc, "color-params");
  BOOST_CHECK(!ColorParams(el).blackWhiteOptions().getColorSegmenterOptions().isPosterizeEnabled());
  BOOST_CHECK(ColorParams(el).colorCommonOptions().getPosterizationOptions().isEnabled());
}

// A gray image is filled with the gray of the color, not with its blue byte.
BOOST_AUTO_TEST_CASE(gray_image_margins_get_the_gray_of_the_color) {
  QImage image(8, 4, QImage::Format_Indexed8);
  QList<QRgb> table;
  for (int i = 0; i < 256; ++i) {
    table.append(qRgb(i, i, i));
  }
  image.setColorTable(table);
  image.fill(10);
  imageproc::BinaryImage mask(QSize(8, 4), imageproc::WHITE);
  mask.fill(QRect(0, 0, 4, 4), imageproc::BLACK);  // The content is the left half.

  const QColor color(200, 10, 30);
  fillMarginsInPlace(image, mask, color);
  BOOST_CHECK_EQUAL(image.pixelIndex(1, 1), 10);
  BOOST_CHECK_EQUAL(image.pixelIndex(6, 1), qGray(color.rgb()));
}

BOOST_AUTO_TEST_CASE(rect_picture_zones_end_at_the_picture) {
  // A picture reaching into the last, incomplete word of a line: the padding bits behind the
  // width counted as picture, so the zone went on to the end of the word.
  // Filled white, the padding bits are 0 (white), as after BinaryImage::fromIndexed8().
  imageproc::BinaryImage mask(QSize(40, 10), imageproc::WHITE);
  mask.fill(QRect(36, 0, 4, 10), imageproc::BLACK);
  const std::vector<QRect> areas = findRectAreas(mask, 100);
  BOOST_REQUIRE_EQUAL(areas.size(), 1u);
  BOOST_CHECK_EQUAL(areas[0].right(), 35);
  BOOST_CHECK(mask.rect().contains(areas[0]));
}

BOOST_AUTO_TEST_CASE(trimming_rect_picture_zones_stays_inside_the_area) {
  // A picture up to the right edge of a mask 96 pixels wide, its last column mostly not part of
  // it. The trimming read the column behind the area, i.e. the first pixel of the next line,
  // which is white here (a picture of its own, apart from the other one).
  imageproc::BinaryImage mask(QSize(96, 20), imageproc::BLACK);
  mask.fill(QRect(72, 0, 23, 20), imageproc::WHITE);
  mask.setPixel(95, 0, imageproc::WHITE);
  mask.fill(QRect(0, 0, 1, 20), imageproc::WHITE);
  const std::vector<QRect> areas = findRectAreas(mask, 50);
  bool found = false;
  for (const QRect& area : areas) {
    if (area.left() >= 64) {
      found = true;
      BOOST_CHECK_EQUAL(area.right(), 94);
    }
  }
  BOOST_CHECK(found);
}

// The left and right edges are trimmed to the first column the picture fills to the given share
// of the rectangle's height, whatever its shape. A short stub at the left of the picture is joined
// into its rectangle and must be trimmed off.
BOOST_AUTO_TEST_CASE(sensitivity_trims_the_sides_by_the_share_of_the_height) {
  struct Case {
    const char* name;
    QSize maskSize;
    QRect picture;
    QRect stub;  // 20 % of the height for the wide picture, 30 % for the tall one.
  };
  const Case cases[] = {
      {"wide", QSize(300, 100), QRect(40, 20, 200, 50), QRect(30, 20, 4, 10)},
      {"tall", QSize(150, 260), QRect(40, 20, 50, 200), QRect(30, 20, 4, 60)},
  };
  for (const Case& c : cases) {
    BOOST_TEST_CONTEXT(c.name) {
      imageproc::BinaryImage mask(c.maskSize, imageproc::BLACK);
      mask.fill(c.picture, imageproc::WHITE);
      mask.fill(c.stub, imageproc::WHITE);
      const std::vector<QRect> areas = findRectAreas(mask, 50);
      BOOST_REQUIRE_EQUAL(areas.size(), 1u);
      BOOST_CHECK_EQUAL(areas[0].left(), c.picture.left());
      BOOST_CHECK_EQUAL(areas[0].right(), c.picture.right());
    }
  }
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
