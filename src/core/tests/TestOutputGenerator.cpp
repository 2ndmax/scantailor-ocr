// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The rules of the Output step, checked on a small synthetic page: a "text" half with black
// strokes and a "picture" half with a gradient. The tests check properties of the result, not
// bytes, so they hold on every platform.

#include <BinaryImage.h>
#include <Constants.h>
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
#include <filters/output/ColorCommonOptions.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DepthPerception.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/FillColorProperty.h>
#include <filters/output/OutputGenerator.h>
#include <filters/output/OutputImage.h>
#include <filters/output/OutputImageWithForeground.h>
#include <filters/output/Params.h>
#include <filters/output/PictureLayerProperty.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/output/Settings.h>
#include <filters/output/SplittingOptions.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>
#include <zones/ZoneSet.h>

#include <QDomDocument>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <memory>
#include <optional>
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

ZoneSet fillZone(const QRect& rect, const QRgb color) {
  PropertySet props;
  props.locateOrCreate<FillColorProperty>()->setColor(color);
  ZoneSet zones;
  zones.add(Zone(SerializableSpline(QPolygonF(QRectF(rect))), props));
  return zones;
}

dewarping::DistortionModel undistortedModel(const QRectF& content) {
  dewarping::DistortionModel model;
  model.setTopCurve(dewarping::Curve(std::vector<QPointF>{content.topLeft(), content.topRight()}));
  model.setBottomCurve(dewarping::Curve(std::vector<QPointF>{content.bottomLeft(), content.bottomRight()}));
  BOOST_REQUIRE(model.isValid());
  return model;
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
  // A valid model to use instead: no deformation, but the dewarping path is taken.
  std::optional<dewarping::DistortionModel> manualModel;
  bool trapezoidDewarping = false;
  bool blankPage = false;
  // Replaces the synthetic page, if set.
  QImage page;
  std::optional<DewarpingMode> dewarpingMode;
  bool evenOutLetterWidths = false;
  bool fillMargins = true;
  double postRotation = 0.0;
  // Gets the distortion model after the processing, if set.
  dewarping::DistortionModel* resultingModel = nullptr;
  // Gets whether the dewarping found no model, if set.
  bool* noModelFound = nullptr;
  BinarizationMethod binarization = T_OTSU;
  // Values as read from a project file (attribute values); empty keeps the defaults of the test.
  QString windowSizeAttribute;
  QString outputDpiAttribute;
  // Gets the size the generator announces for the output, if set.
  QSize* announcedSize = nullptr;
  // Fill zones in original image coordinates. Empty by default, like a project without zones.
  ZoneSet fillZones;
  // Split output, despeckling and rectangular picture detection, as in a project file.
  bool splitOutput = false;
  double despeckleLevel = 0.0;
  PictureShape pictureShape = OFF_SHAPE;
  // Only set when a test asks, so BlackWhiteOptions and ColorCommonOptions keep their own defaults.
  std::optional<bool> normalizeIlluminationBW;
  std::optional<bool> normalizeIlluminationColor;
  std::optional<FillingColor> fillingColor;
  bool blackOnWhite = true;
  bool takeOverBlackOnWhite = true;
  // Receives the images the generator writes as a side effect, if set.
  imageproc::BinaryImage* autoPictureMask = nullptr;
  imageproc::BinaryImage* specklesImage = nullptr;
  std::shared_ptr<Settings>* settingsOut = nullptr;
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
  blackWhiteOptions.setBinarizationMethod(run.binarization);
  blackWhiteOptions.setLowerBound(run.lowerBound);
  blackWhiteOptions.setUpperBound(run.upperBound);
  if (run.normalizeIlluminationBW) {
    blackWhiteOptions.setNormalizeIllumination(*run.normalizeIlluminationBW);
  }
  if (run.kmeansColors > 0) {
    BlackWhiteOptions::ColorSegmenterOptions segmenterOptions = blackWhiteOptions.getColorSegmenterOptions();
    segmenterOptions.setEnabled(true);
    segmenterOptions.setKMeansEnabled(true);
    segmenterOptions.setKMeansColors(run.kmeansColors);
    blackWhiteOptions.setColorSegmenterOptions(segmenterOptions);
  }
  if (!run.windowSizeAttribute.isEmpty()) {
    QDomDocument doc;
    QDomElement el = blackWhiteOptions.toXml(doc, QStringLiteral("black-white-options"));
    QDomElement methodEl = el.firstChildElement(QStringLiteral("method"));
    methodEl.setAttribute(QStringLiteral("window"), run.windowSizeAttribute);
    blackWhiteOptions = BlackWhiteOptions(el);
  }
  colorParams.setBlackWhiteOptions(blackWhiteOptions);
  params.setColorParams(colorParams);
  // By default only a manual zone counts as a picture, so the results don't depend on picture
  // detection. Individual tests opt into the automatic shapes.
  PictureShapeOptions shapeOptions;
  shapeOptions.setPictureShape(run.pictureShape);
  params.setPictureShapeOptions(shapeOptions);
  SplittingOptions splittingOptions;
  splittingOptions.setSplitOutput(run.splitOutput);
  params.setSplittingOptions(splittingOptions);
  params.setDespeckleLevel(run.despeckleLevel);
  params.setBlackOnWhite(run.blackOnWhite);
  if (run.thinManualDewarping || run.manualModel) {
    params.setDewarpingOptions(DewarpingOptions(MANUAL));
  }
  if (run.trapezoidDewarping) {
    params.setDewarpingOptions(DewarpingOptions(TRAPEZOID));
  }
  if (run.dewarpingMode) {
    DewarpingOptions dewarpingOptions(*run.dewarpingMode);
    dewarpingOptions.setEvenOutLetterWidths(run.evenOutLetterWidths);
    params.setDewarpingOptions(dewarpingOptions);
  }
  ColorCommonOptions colorCommonOptions = params.colorParams().colorCommonOptions();
  colorCommonOptions.setFillMargins(run.fillMargins);
  if (run.normalizeIlluminationColor) {
    colorCommonOptions.setNormalizeIllumination(*run.normalizeIlluminationColor);
  }
  if (run.fillingColor) {
    colorCommonOptions.setFillingColor(*run.fillingColor);
  }
  ColorParams colorParamsWithMargins = params.colorParams();
  colorParamsWithMargins.setColorCommonOptions(colorCommonOptions);
  params.setColorParams(colorParamsWithMargins);
  settings->setParams(pageId, params);

  QImage page = run.page.isNull() ? syntheticPage(run.colored, run.strokeColor) : run.page;
  if (run.blankPage) {
    page.fill(Qt::white);
  }
  const FilterData data(page);
  ImageTransformation xform(data.xform());
  xform.setPostRotation(run.postRotation);
  xform.postScaleToDpi(outputDpi);
  if (run.postScaleFactor != 1.0) {
    xform.setPostScaleFactor(run.postScaleFactor);
  }
  const OutputGenerator generator(xform, QPolygonF(run.content));
  if (run.announcedSize) {
    *run.announcedSize = generator.outputImageSize();
  }

  ZoneSet pictureZones = run.withPictureZone ? pictureZone() : ZoneSet();
  dewarping::DistortionModel distortionModel;
  if (run.manualModel) {
    distortionModel = *run.manualModel;
  } else if (run.thinManualDewarping) {
    distortionModel.setTopCurve(dewarping::Curve(std::vector<QPointF>{QPointF(100, 100), QPointF(100.3, 100)}));
    distortionModel.setBottomCurve(dewarping::Curve(std::vector<QPointF>{QPointF(100, 200), QPointF(100.3, 200)}));
    BOOST_REQUIRE(distortionModel.isValid());
  }
  std::unique_ptr<OutputImage> output
      = generator.process(NullTaskStatus(), data, pictureZones, run.fillZones, distortionModel, DepthPerception(),
                          run.autoPictureMask, run.specklesImage, nullptr, pageId, settings, run.takeOverBlackOnWhite);
  if (run.noModelFound) {
    *run.noModelFound = settings->getParams(pageId).dewarpingOptions().noModelFound();
  }
  if (run.resultingModel) {
    *run.resultingModel = distortionModel;
  }
  if (run.settingsOut) {
    *run.settingsOut = settings;
  }
  return output;
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
  BlackWhiteOptions sauvola;
  sauvola.setBinarizationMethod(T_SAUVOLA);
  QDomElement optionsEl = sauvola.toXml(doc, QStringLiteral("black-white-options"));
  QDomElement methodEl = optionsEl.firstChildElement(QStringLiteral("method"));
  methodEl.setAttribute(QStringLiteral("window"), QStringLiteral("3"));
  BOOST_CHECK_EQUAL(BlackWhiteOptions(optionsEl).values().windowSize, 3);
  methodEl.setAttribute(QStringLiteral("window"), QStringLiteral("2"));
  BOOST_CHECK_EQUAL(BlackWhiteOptions(optionsEl).values().windowSize, BlackWhiteOptions::DEFAULT_WINDOW_SIZE);
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

// Margin colors for every filling color, polarity and output mode. The picture half lies outside
// the content box, so it shows only the margin color. The scan itself is black on white; with
// blackOnWhite == false the polarity comes from the parameters, not from the scan. Characterized
// as they are today: in black-and-white output the filling color makes no visible difference.
BOOST_AUTO_TEST_CASE(filling_color_decides_the_margins_in_both_paths) {
  struct FillingCase {
    const char* name;
    std::optional<FillingColor> fillingColor;  // Empty keeps the default of ColorCommonOptions.
  };
  const FillingCase cases[] = {{"default", std::nullopt}, {"white", FILL_WHITE}, {"black", FILL_BLACK}};

  for (const ColorMode mode : {BLACK_AND_WHITE, COLOR_GRAYSCALE, MIXED}) {
    for (const bool dewarping : {false, true}) {
      for (const bool blackOnWhite : {true, false}) {
        for (const FillingCase& fillingCase : cases) {
          BOOST_TEST_CONTEXT("mode " << mode << ", dewarping " << dewarping << ", blackOnWhite " << blackOnWhite
                                     << ", filling color " << fillingCase.name) {
            Run run;
            run.mode = mode;
            run.content = QRectF(TEXT_RECT);
            run.takeOverBlackOnWhite = false;
            run.blackOnWhite = blackOnWhite;
            if (dewarping) {
              run.manualModel = undistortedModel(QRectF(0, 0, WIDTH, HEIGHT));
            }
            run.fillingColor = fillingCase.fillingColor;
            run.withPictureZone = (mode == MIXED);
            const QImage output = process(run);
            const std::set<int> textLevels = grayLevels(output, inner(TEXT_RECT));
            BOOST_REQUIRE_GE(textLevels.size(), 2u);
            BOOST_CHECK_LT(*textLevels.begin(), 100);
            BOOST_CHECK_GT(*textLevels.rbegin(), 150);
            const std::set<int> levels = grayLevels(output, inner(PICTURE_RECT));
            BOOST_REQUIRE_EQUAL(levels.size(), 1u);
            const int level = *levels.begin();
            if (mode == BLACK_AND_WHITE) {
              // Binarized margins end up white on black on white and black on white on black,
              // whatever the filling color says.
              BOOST_CHECK_EQUAL(level, blackOnWhite ? 255 : 0);
            } else if (mode == MIXED) {
              // Mixed output also binarizes. The switch maps White and Black to the final polarity;
              // the default starts white and is inverted back with a white-on-black page.
              if (fillingCase.fillingColor && *fillingCase.fillingColor == FILL_WHITE) {
                BOOST_CHECK_EQUAL(level, 255);
              } else if (fillingCase.fillingColor && *fillingCase.fillingColor == FILL_BLACK) {
                BOOST_CHECK_EQUAL(level, 0);
              } else {
                BOOST_CHECK_EQUAL(level, blackOnWhite ? 255 : 0);
              }
            } else if (fillingCase.fillingColor && *fillingCase.fillingColor == FILL_WHITE) {
              BOOST_CHECK_EQUAL(level, 255);
            } else if (fillingCase.fillingColor && *fillingCase.fillingColor == FILL_BLACK) {
              BOOST_CHECK_EQUAL(level, 0);
            } else if (blackOnWhite) {
              BOOST_CHECK_GE(level, 245);  // The paper's own background.
            } else {
              BOOST_CHECK_LE(level, 10);  // That same background, inverted with the page.
            }
          }
        }
      }
    }
  }
}

// Fill zones are painted after binarization and dewarping, over text as well as over pictures.
// Their interior has one color; pixels outside the zones keep the pixels of a run without zones.
BOOST_AUTO_TEST_CASE(fill_zones_cover_text_paper_and_pictures_in_every_mode) {
  const QRect whiteZoneRect(40, 40, 60, 60);     // Over the strokes in the text half.
  const QRect blackZoneRect(250, 290, 60, 40);   // Over the light paper below the picture.
  const QRect pictureZoneRect(240, 60, 60, 60);  // Inside the picture zone of a mixed page.
  const QRect untouchedText(40, 140, 60, 60);
  const QRect untouchedPaper(320, 290, 60, 40);

  for (const ColorMode mode : {BLACK_AND_WHITE, COLOR_GRAYSCALE, MIXED}) {
    for (const bool dewarping : {false, true}) {
      BOOST_TEST_CONTEXT("mode " << mode << ", dewarping " << dewarping) {
        Run run;
        run.mode = mode;
        if (dewarping) {
          run.manualModel = undistortedModel(QRectF(0, 0, WIDTH, HEIGHT));
        }
        run.withPictureZone = (mode == MIXED);
        const QImage withoutZones = process(run);

        run.fillZones = fillZone(whiteZoneRect, qRgb(255, 255, 255));
        const ZoneSet blackZone = fillZone(blackZoneRect, qRgb(0, 0, 0));
        for (const Zone& zone : blackZone) {
          run.fillZones.add(zone);
        }
        if (mode == MIXED) {
          const ZoneSet overPicture = fillZone(pictureZoneRect, qRgb(255, 255, 255));
          for (const Zone& zone : overPicture) {
            run.fillZones.add(zone);
          }
        }
        const QImage withZones = process(run);

        BOOST_CHECK(grayLevels(withZones, inner(whiteZoneRect)) == (std::set<int>{255}));
        BOOST_CHECK(grayLevels(withZones, inner(blackZoneRect)) == (std::set<int>{0}));
        BOOST_CHECK(samePixels(withZones, withoutZones, untouchedText));
        BOOST_CHECK(samePixels(withZones, withoutZones, untouchedPaper));
        if (mode == MIXED) {
          BOOST_CHECK(grayLevels(withZones, inner(pictureZoneRect)) == (std::set<int>{255}));
        }
      }
    }
  }
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
BOOST_AUTO_TEST_CASE(test_trapezoid_dewarping_of_a_page_without_lines_reports_no_model) {
  Run run;
  run.trapezoidDewarping = true;
  run.blankPage = true;
  bool noModelFound = false;
  run.noModelFound = &noModelFound;
  const std::unique_ptr<OutputImage> output = generate(run);
  BOOST_REQUIRE(output && !output->isNull());
  BOOST_TEST(noModelFound);
}

namespace {
const QSize A4_PAGE_SIZE(2480, 3508);
const int TEXT_LEFT = 300;
const int TEXT_RIGHT = 2180;

QImage whiteA4Page() {
  QImage page(A4_PAGE_SIZE, QImage::Format_RGB32);
  page.fill(Qt::white);
  const Dpm dpm(DPI);
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  return page;
}

/**
 * Draws a line of dark letters, grouped into words of pseudo-random length, from \p left to exactly \p right.
 */
void drawWords(QPainter& painter, const int left, const int right, const int y) {
  const int letterWidth = 14;
  const int letterHeight = 32;
  const int gapBetweenLetters = 8;
  const int gapBetweenWords = 24;
  unsigned int random = 12345 + static_cast<unsigned int>(y);
  int lettersLeftInWord = 5;
  int x = left;
  while (x + letterWidth < right) {
    painter.fillRect(x, y, letterWidth, letterHeight, Qt::black);
    x += letterWidth + gapBetweenLetters;
    if (--lettersLeftInWord == 0) {
      random = random * 1103515245 + 12345;
      lettersLeftInWord = 3 + (random >> 16) % 7;
      x += gapBetweenWords;
    }
  }
  painter.fillRect(right - letterWidth, y, letterWidth, letterHeight, Qt::black);
}

/** A line of "H" shaped letters that get narrower towards the right edge, as near the spine of a book. */
void drawLettersNarrowingToTheRight(QPainter& painter, const int y) {
  const int narrowingStart = 1400;
  double x = TEXT_LEFT;
  while (x < TEXT_RIGHT) {
    const double scale = (x < narrowingStart) ? 1.0 : 1.0 - 0.25 * (x - narrowingStart) / (TEXT_RIGHT - narrowingStart);
    const int stem = static_cast<int>(std::lround(8 * scale));
    const int width = static_cast<int>(std::lround(40 * scale));
    painter.fillRect(static_cast<int>(x), y, stem, 60, Qt::black);
    painter.fillRect(static_cast<int>(x) + width - stem, y, stem, 60, Qt::black);
    painter.fillRect(static_cast<int>(x), y + 27, width, 6, Qt::black);
    x += 60 * scale;
  }
}

double edgeTiltInDegrees(const QPointF& top, const QPointF& bottom) {
  return std::atan2(bottom.x() - top.x(), bottom.y() - top.y()) * 180.0 / constants::PI;
}

double leftEdgeTilt(const dewarping::DistortionModel& model) {
  return edgeTiltInDegrees(model.topCurve().polyline().front(), model.bottomCurve().polyline().front());
}

double rightEdgeTilt(const dewarping::DistortionModel& model) {
  return edgeTiltInDegrees(model.topCurve().polyline().back(), model.bottomCurve().polyline().back());
}

bool closeTo(const QPointF& a, const QPointF& b) {
  const double tolerance = 3.0;
  return (std::abs(a.x() - b.x()) < tolerance) && (std::abs(a.y() - b.y()) < tolerance);
}

bool sameModel(const dewarping::DistortionModel& a, const dewarping::DistortionModel& b) {
  return closeTo(a.topCurve().polyline().front(), b.topCurve().polyline().front())
         && closeTo(a.topCurve().polyline().back(), b.topCurve().polyline().back())
         && closeTo(a.bottomCurve().polyline().front(), b.bottomCurve().polyline().front())
         && closeTo(a.bottomCurve().polyline().back(), b.bottomCurve().polyline().back());
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_filling_the_margins_doesnt_decide_how_a_page_is_dewarped) {
  // A block of text and, far below it, a short footnote, inside a content box drawn by hand.
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    for (int line = 0; line < 18; ++line) {
      drawWords(painter, TEXT_LEFT, TEXT_RIGHT, 400 + line * 70);
    }
    drawWords(painter, TEXT_LEFT, TEXT_LEFT + 700, 3150);
    drawWords(painter, TEXT_LEFT, TEXT_LEFT + 700, 3236);
  }

  for (const DewarpingMode mode : {AUTO, TRAPEZOID}) {
    Run run;
    run.page = page;
    run.content = QRectF(260, 384, 1960, 2860);
    run.dewarpingMode = mode;

    dewarping::DistortionModel filledModel;
    bool filledNoModel = false;
    run.fillMargins = true;
    run.resultingModel = &filledModel;
    run.noModelFound = &filledNoModel;
    generate(run);

    dewarping::DistortionModel unfilledModel;
    bool unfilledNoModel = false;
    run.fillMargins = false;
    run.resultingModel = &unfilledModel;
    run.noModelFound = &unfilledNoModel;
    generate(run);

    BOOST_TEST(filledNoModel == unfilledNoModel);
    BOOST_TEST(sameModel(filledModel, unfilledModel));
  }
}

BOOST_AUTO_TEST_CASE(test_both_edges_of_a_tilted_text_block_keep_their_tilt) {
  // Horizontal lines whose ends all lie on edges tilted by 3.5 degrees: within what the tracer accepts.
  const double shiftPerRow = std::tan(3.5 * constants::PI / 180.0);
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    for (int line = 0; line < 40; ++line) {
      const int y = 300 + line * 70;
      const int shift = static_cast<int>(std::lround((y - 300) * shiftPerRow));
      drawWords(painter, TEXT_LEFT + shift, TEXT_RIGHT + shift - 200, y);
    }
  }
  Run run;
  run.page = page;
  run.content = QRectF(QPointF(-200, -200), A4_PAGE_SIZE + QSize(400, 400));
  run.dewarpingMode = AUTO;
  bool noModelFound = true;
  dewarping::DistortionModel model;
  run.noModelFound = &noModelFound;
  run.resultingModel = &model;
  generate(run);

  BOOST_REQUIRE(!noModelFound);
  BOOST_TEST(std::abs(leftEdgeTilt(model) - rightEdgeTilt(model)) < 1.0);
}

BOOST_AUTO_TEST_CASE(test_both_edges_of_a_crookedly_scanned_page_keep_their_tilt) {
  // Upright text, scanned 3 degrees crooked and turned back by Deskew. In the scan both edges are tilted alike.
  const double scanAngle = 3.0;
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    painter.translate(A4_PAGE_SIZE.width() / 2.0, A4_PAGE_SIZE.height() / 2.0);
    painter.rotate(scanAngle);
    painter.translate(-A4_PAGE_SIZE.width() / 2.0, -A4_PAGE_SIZE.height() / 2.0);
    for (int line = 0; line < 40; ++line) {
      drawWords(painter, TEXT_LEFT, TEXT_RIGHT, 300 + line * 70);
    }
  }
  Run run;
  run.page = page;
  run.content = QRectF(QPointF(-200, -200), A4_PAGE_SIZE + QSize(400, 400));
  run.dewarpingMode = AUTO;
  run.postRotation = -scanAngle;
  bool noModelFound = true;
  dewarping::DistortionModel model;
  run.noModelFound = &noModelFound;
  run.resultingModel = &model;
  generate(run);

  BOOST_REQUIRE(!noModelFound);
  BOOST_TEST(std::abs(leftEdgeTilt(model) - rightEdgeTilt(model)) < 1.0);
}

namespace {
bool narrowingLettersPageFindsNoModel(const DewarpingMode mode,
                                      const int lines,
                                      const int lineStep,
                                      dewarping::DistortionModel& model) {
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    for (int line = 0; line < lines; ++line) {
      drawLettersNarrowingToTheRight(painter, 300 + line * lineStep);
    }
  }
  Run run;
  run.page = page;
  run.content = QRectF(QPointF(0, 0), A4_PAGE_SIZE);
  run.dewarpingMode = mode;
  run.evenOutLetterWidths = true;
  bool noModelFound = false;
  run.noModelFound = &noModelFound;
  run.resultingModel = &model;
  generate(run);
  return noModelFound;
}
}  // namespace

BOOST_AUTO_TEST_CASE(test_letter_widths_are_left_alone_on_a_page_without_a_model) {
  // Seven lines are too few for straight lines, with "Auto" and with "Trapezoid", and a short block of them is
  // too little for curved ones.
  const struct {
    DewarpingMode mode;
    int lines;
    int lineStep;
  } cases[] = {{AUTO, 7, 100}, {TRAPEZOID, 7, 400}};

  for (const auto& pageCase : cases) {
    dewarping::DistortionModel model;
    BOOST_REQUIRE(narrowingLettersPageFindsNoModel(pageCase.mode, pageCase.lines, pageCase.lineStep, model));
    BOOST_TEST(model.widthCorrection().isNull());
  }
}

BOOST_AUTO_TEST_CASE(test_letter_widths_are_evened_out_on_a_page_with_a_model) {
  // Shows that the pages of the test above could get a correction at all.
  dewarping::DistortionModel model;
  BOOST_REQUIRE(!narrowingLettersPageFindsNoModel(AUTO, 27, 100, model));
  BOOST_TEST(!model.widthCorrection().isNull());
}

namespace {
// A large mark that despeckling keeps, and a tiny isolated one that it removes. Both are far
// enough apart that the removed one cannot be kept as a neighbour of the large one.
const QRect MARK_RECT(40, 40, 60, 60);
const QRect SPECKLE_RECT(350, 320, 3, 3);

QImage pageWithMarkAndSpeckle() {
  QImage page(WIDTH, HEIGHT, QImage::Format_RGB32);
  page.fill(qRgb(255, 255, 255));
  const Dpm dpm(DPI);
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  QPainter painter(&page);
  painter.fillRect(MARK_RECT, Qt::black);
  painter.fillRect(SPECKLE_RECT, Qt::black);
  return page;
}

QImage pageWithMarkSpeckleAndPicture() {
  QImage page = pageWithMarkAndSpeckle();
  for (int y = PICTURE_RECT.top(); y <= PICTURE_RECT.bottom(); ++y) {
    for (int x = PICTURE_RECT.left(); x <= PICTURE_RECT.right(); ++x) {
      const int v = 60 + (x - PICTURE_RECT.left()) * 140 / PICTURE_RECT.width();
      page.setPixel(x, y, qRgb(v, v, v));
    }
  }
  return page;
}

void checkBinaryPixels(const imageproc::BinaryImage& image, const QImage& expected) {
  BOOST_REQUIRE(image.size() == expected.size());
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      const bool expectedBlack = qGray(expected.pixel(x, y)) < 128;
      BOOST_TEST_CONTEXT("x=" << x << " y=" << y) {
        BOOST_CHECK_EQUAL(image.getPixel(x, y) == imageproc::BLACK, expectedBlack);
      }
    }
  }
}

void checkImagePixels(const QImage& image, const QImage& expected) {
  BOOST_REQUIRE(image.size() == expected.size());
  for (int y = 0; y < expected.height(); ++y) {
    for (int x = 0; x < expected.width(); ++x) {
      BOOST_TEST_CONTEXT("x=" << x << " y=" << y) {
        BOOST_CHECK(image.pixel(x, y) == expected.pixel(x, y));
      }
    }
  }
}
}  // namespace

BOOST_AUTO_TEST_CASE(split_output_and_speckles_image_keep_their_pixels) {
  Run run;
  run.page = pageWithMarkAndSpeckle();
  // Split output is only enabled for mixed pages; its foreground is the black-and-white layer.
  run.mode = MIXED;
  run.splitOutput = true;
  run.despeckleLevel = 2.0;
  imageproc::BinaryImage speckles(QSize(1, 1), imageproc::WHITE);
  run.specklesImage = &speckles;
  const std::unique_ptr<OutputImage> output = generate(run);
  BOOST_REQUIRE(output && !output->isNull());

  const auto* splitOutput = dynamic_cast<const OutputImageWithForeground*>(output.get());
  BOOST_REQUIRE(splitOutput != nullptr);
  BOOST_CHECK(splitOutput->getForegroundImage().size() == QSize(WIDTH, HEIGHT));
  BOOST_CHECK(splitOutput->getBackgroundImage().size() == QSize(WIDTH, HEIGHT));

  QImage expectedForeground(WIDTH, HEIGHT, QImage::Format_RGB32);
  expectedForeground.fill(qRgb(255, 255, 255));
  {
    QPainter painter(&expectedForeground);
    painter.fillRect(MARK_RECT, Qt::black);
  }
  QImage expectedBackground(WIDTH, HEIGHT, QImage::Format_RGB32);
  expectedBackground.fill(qRgb(255, 255, 255));
  checkImagePixels(splitOutput->getForegroundImage(), expectedForeground);
  checkImagePixels(splitOutput->getBackgroundImage(), expectedBackground);

  QImage expectedSpeckles(WIDTH, HEIGHT, QImage::Format_RGB32);
  expectedSpeckles.fill(qRgb(255, 255, 255));
  {
    QPainter painter(&expectedSpeckles);
    painter.fillRect(SPECKLE_RECT, Qt::black);
  }
  checkBinaryPixels(speckles, expectedSpeckles);

  Run withoutDespeckling = run;
  withoutDespeckling.despeckleLevel = 0.0;
  withoutDespeckling.specklesImage = nullptr;
  const QImage withSpeckle = process(withoutDespeckling);
  const QImage withoutSpeckle = output->toImage().convertToFormat(QImage::Format_RGB32);
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      BOOST_TEST_CONTEXT("x=" << x << " y=" << y) {
        if (SPECKLE_RECT.contains(x, y)) {
          BOOST_CHECK_EQUAL(qGray(withoutSpeckle.pixel(x, y)), 255);
        } else {
          BOOST_CHECK_EQUAL(withoutSpeckle.pixel(x, y), withSpeckle.pixel(x, y));
        }
      }
    }
  }
}

BOOST_AUTO_TEST_CASE(automatic_picture_mask_and_speckles_keep_their_pixels) {
  Run run;
  run.page = pageWithMarkSpeckleAndPicture();
  run.mode = MIXED;
  run.pictureShape = FREE_SHAPE;
  run.despeckleLevel = 2.0;
  imageproc::BinaryImage mask(QSize(1, 1), imageproc::WHITE);
  imageproc::BinaryImage speckles(QSize(1, 1), imageproc::WHITE);
  run.autoPictureMask = &mask;
  run.specklesImage = &speckles;
  generate(run);

  QImage expectedSpeckles(WIDTH, HEIGHT, QImage::Format_RGB32);
  expectedSpeckles.fill(qRgb(255, 255, 255));
  {
    QPainter painter(&expectedSpeckles);
    painter.fillRect(SPECKLE_RECT, Qt::black);
  }
  checkBinaryPixels(speckles, expectedSpeckles);

  // Automatic detection sees the gradient as the only picture. In the current mask the detected
  // area is white and everything else, including the black mark, is black. The detector's output
  // is scaled back with one pixel of overflow on each side.
  QImage expectedMask(WIDTH, HEIGHT, QImage::Format_RGB32);
  expectedMask.fill(qRgb(0, 0, 0));
  {
    QPainter painter(&expectedMask);
    painter.fillRect(PICTURE_RECT.adjusted(-1, -1, 1, 1), Qt::white);
  }
  checkBinaryPixels(mask, expectedMask);
}

BOOST_AUTO_TEST_CASE(taking_over_black_on_white_only_changes_that_field) {
  const PageId pageId(ImageId(QStringLiteral("page.png")), PageId::SINGLE_PAGE);
  Run run;
  run.blackOnWhite = false;

  // The run without take-over gives us the parameters exactly as the generator received them.
  Run withoutTakeOver = run;
  withoutTakeOver.takeOverBlackOnWhite = false;
  std::shared_ptr<Settings> beforeSettings;
  withoutTakeOver.settingsOut = &beforeSettings;
  generate(withoutTakeOver);
  BOOST_REQUIRE(beforeSettings);
  const Params before = beforeSettings->getParams(pageId);
  BOOST_REQUIRE(!before.isBlackOnWhite());

  std::shared_ptr<Settings> afterSettings;
  run.settingsOut = &afterSettings;
  generate(run);
  BOOST_REQUIRE(afterSettings);
  const Params after = afterSettings->getParams(pageId);
  BOOST_CHECK(after.isBlackOnWhite());
  QDomDocument beforeDoc;
  QDomDocument afterDoc;
  QDomElement beforeEl = before.toXml(beforeDoc, QStringLiteral("params"));
  beforeDoc.appendChild(beforeEl);
  beforeEl.setAttribute(QStringLiteral("blackOnWhite"), QStringLiteral("1"));
  afterDoc.appendChild(after.toXml(afterDoc, QStringLiteral("params")));
  BOOST_CHECK(beforeDoc.toString() == afterDoc.toString());
}

BOOST_AUTO_TEST_CASE(rectangular_picture_detection_runs_through_process) {
  Run off;
  off.mode = MIXED;
  const QImage withoutDetection = process(off);
  BOOST_CHECK_LT(grayLevels(withoutDetection, inner(PICTURE_RECT)).size(), 3u);

  Run rectangular = off;
  rectangular.pictureShape = RECTANGULAR_SHAPE;
  const QImage withDetection = process(rectangular);
  BOOST_CHECK_GT(grayLevels(withDetection, inner(PICTURE_RECT)).size(), 50u);
}

// Normalization with unfilled margins estimates the background over the whole working area, so
// the uneven light and the dark scan edge below the text are part of the estimate before Auto
// dewarping traces the lines. The resulting model is non-trivial and follows the slanted text
// block. Its edges are checked with a tolerance; see the REVIEW-NOTE in OutputGeneratorDewarping.
BOOST_AUTO_TEST_CASE(unfilled_margins_and_normalized_light_keep_the_traced_model) {
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    for (int y = 0; y < page.height(); ++y) {
      const int shade = 250 - y * 170 / page.height();
      painter.fillRect(0, y, page.width(), 1, qRgb(shade, shade, shade));
    }
    painter.fillRect(0, page.height() - 20, page.width(), 20, Qt::black);
    for (int line = 0; line < 24; ++line) {
      const double t = line / 23.0;
      const int left = TEXT_LEFT + static_cast<int>(100 * t);
      const int right = TEXT_RIGHT + static_cast<int>(120 * t);
      drawWords(painter, left, right, 500 + line * 100);
    }
  }

  Run run;
  run.page = page;
  run.content = QRectF(260, 384, 1960, 2860);
  run.dewarpingMode = AUTO;
  run.normalizeIlluminationBW = true;
  run.normalizeIlluminationColor = true;
  run.fillMargins = false;
  bool noModelFound = true;
  dewarping::DistortionModel model;
  run.noModelFound = &noModelFound;
  run.resultingModel = &model;
  generate(run);

  BOOST_REQUIRE(!noModelFound);
  BOOST_REQUIRE(model.isValid());
  const auto& top = model.topCurve().polyline();
  const auto& bottom = model.bottomCurve().polyline();
  BOOST_REQUIRE_EQUAL(top.size(), 2u);
  BOOST_REQUIRE_EQUAL(bottom.size(), 2u);
  // About a millimetre at 300 dpi is 12 pixels. The model follows the text, not the page edges.
  const auto near = [](const QPointF& point, const QPointF& expected) {
    return (std::abs(point.x() - expected.x()) < 12.0) && (std::abs(point.y() - expected.y()) < 12.0);
  };
  BOOST_TEST(near(top.front(), QPointF(296, 515)));
  BOOST_TEST(near(top.back(), QPointF(2206, 515)));
  BOOST_TEST(near(bottom.front(), QPointF(387, 2815)));
  BOOST_TEST(near(bottom.back(), QPointF(2255, 2815)));
}

// With very little text there is nothing for marginal dewarping to trace, so it keeps the trivial
// model even with normalized illumination and unfilled margins. The REVIEW-NOTE in
// OutputGeneratorDewarping.cpp is about the traced Auto/Trapezoid model, which this test does not
// exercise; the test above covers that path with a non-trivial model.
BOOST_AUTO_TEST_CASE(few_text_lines_with_normalized_light_keep_the_trivial_marginal_model) {
  QImage page = whiteA4Page();
  {
    QPainter painter(&page);
    for (int y = 0; y < page.height(); ++y) {
      const int shade = 250 - y * 60 / page.height();
      painter.fillRect(0, y, page.width(), 1, qRgb(shade, shade, shade));
    }
    painter.fillRect(0, page.height() - 20, page.width(), 20, Qt::black);
    for (int line = 0; line < 3; ++line) {
      drawWords(painter, TEXT_LEFT, TEXT_RIGHT, 800 + line * 100);
    }
  }
  Run run;
  run.page = page;
  run.content = QRectF(QPointF(0, 0), A4_PAGE_SIZE);
  run.dewarpingMode = MARGINAL;
  run.normalizeIlluminationBW = true;
  run.normalizeIlluminationColor = true;
  run.fillMargins = false;
  bool noModelFound = true;
  dewarping::DistortionModel model;
  run.noModelFound = &noModelFound;
  run.resultingModel = &model;
  generate(run);
  BOOST_REQUIRE(!noModelFound);
  const auto& top = model.topCurve().polyline();
  const auto& bottom = model.bottomCurve().polyline();
  BOOST_CHECK(model.isValid());
  BOOST_TEST(closeTo(top.front(), QPointF(0, 0)));
  BOOST_TEST(closeTo(top.back(), QPointF(A4_PAGE_SIZE.width(), 0)));
  BOOST_TEST(closeTo(bottom.front(), QPointF(0, A4_PAGE_SIZE.height())));
  BOOST_TEST(closeTo(bottom.back(), QPointF(A4_PAGE_SIZE.width(), A4_PAGE_SIZE.height())));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
