// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The rules of the Output step, checked on a small synthetic page: a "text" half with black
// strokes and a "picture" half with a gradient.  The tests check properties of the result, not
// bytes, so they hold on every platform.

#include <DistortionModel.h>
#include <Dpi.h>
#include <Dpm.h>
#include <FilterData.h>
#include <ImageTransformation.h>
#include <NullTaskStatus.h>
#include <PageId.h>
#include <PropertySet.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DepthPerception.h>
#include <filters/output/OutputGenerator.h>
#include <filters/output/OutputImage.h>
#include <filters/output/Params.h>
#include <filters/output/PictureLayerProperty.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/output/Settings.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>
#include <zones/ZoneSet.h>

#include <QImage>
#include <QPolygonF>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <set>

namespace Tests {
using namespace output;

namespace {
const int WIDTH = 400;
const int HEIGHT = 300;
const Dpi DPI(300, 300);
// The picture half, in source image coordinates, with some distance to the text half.
const QRect PICTURE_RECT(220, 20, 160, 260);
// Inside the text half, where only black strokes on white are.
const QRect TEXT_RECT(20, 20, 160, 260);

// Black strokes on the left half, a gradient on the right half; light gray elsewhere.
QImage syntheticPage(const bool colored = false) {
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
  double postScaleFactor = 1.0;
  QRectF content = QRectF(0, 0, WIDTH, HEIGHT);
};

QImage process(const Run& run) {
  const PageId pageId(ImageId(QStringLiteral("page.png")), PageId::SINGLE_PAGE);
  auto settings = std::make_shared<Settings>();
  Params params;
  params.setOutputDpi(DPI);
  ColorParams colorParams;
  colorParams.setColorMode(run.mode);
  colorParams.setPhotoAdjustments(run.adjustments);
  params.setColorParams(colorParams);
  // Only the manual zone counts as a picture, so the results don't depend on picture detection.
  PictureShapeOptions shapeOptions;
  shapeOptions.setPictureShape(OFF_SHAPE);
  params.setPictureShapeOptions(shapeOptions);
  params.setDespeckleLevel(0.0);
  settings->setParams(pageId, params);

  const FilterData data(syntheticPage(run.colored));
  ImageTransformation xform(data.xform());
  xform.postScaleToDpi(DPI);
  if (run.postScaleFactor != 1.0) {
    xform.setPostScaleFactor(run.postScaleFactor);
  }
  const OutputGenerator generator(xform, QPolygonF(run.content));

  ZoneSet pictureZones = run.withPictureZone ? pictureZone() : ZoneSet();
  const ZoneSet fillZones;
  dewarping::DistortionModel distortionModel;
  const std::unique_ptr<OutputImage> output
      = generator.process(NullTaskStatus(), data, pictureZones, fillZones, distortionModel, DepthPerception(), nullptr,
                          nullptr, nullptr, pageId, settings);
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

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
