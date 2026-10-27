// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorMethods.h"

#include "BinaryImage.h"
#include "BinaryThreshold.h"
#include "ColorKMeans.h"
#include "ColorSegmenter.h"
#include "Dpi.h"
#include "ExpColorFilter.h"
#include "GrayImage.h"
#include "Posterizer.h"
#include "tools/ToolImageDpi.h"

namespace binarization_compare {
namespace {
using imageproc::BinaryImage;

// The noise reduction of the color segmentation, as the default of the Output step.
const int kNoiseReduction = 7;

// Each letter in the average color of its pixels, the background white.
QImage segmented(const QImage& color, const QImage& text) {
  const imageproc::ColorSegmenter segmenter(tools::dpiOrDefault(color), kNoiseReduction);
  return segmenter.segment(BinaryImage(text), color.convertToFormat(QImage::Format_RGB32))
      .convertToFormat(QImage::Format_RGB32);
}

QImage posterized(const QImage& color, const QImage& text, const int level) {
  // As the Output step with posterization: the levels and "force black and white".
  return imageproc::Posterizer(level, false, true)
      .posterize(segmented(color, text))
      .convertToFormat(QImage::Format_RGB32);
}

QImage kmeans(const QImage& color, const QImage& text, const int k, const bool fixBlack) {
  return imageproc::reduceColorsKMeans(segmented(color, text), k, fixBlack);
}

// Our K-means on the pixels of the text: each pixel on its own, or letters as one color and only
// larger areas pixel by pixel. A letter is at most as large as a filled square of a quarter inch.
QImage textKMeans(const QImage& color, const QImage& text, const int k, const bool combined) {
  const Dpi dpi = tools::dpiOrDefault(color);
  const int quarterInch = (dpi.horizontal() + dpi.vertical()) / 8;
  return imageproc::reduceTextColorsKMeans(color, BinaryImage(text), k, true, combined ? quarterInch * quarterInch : 0);
}

// The code of ScanTailor Experimental, called as its Output step does in black and white mode: the
// black and white text as the image to color, the whole page as the area, its defaults (HSV, start
// value 128, no saturation, normalization or background colors), with or without "find black".
QImage experimentalKMeans(const QImage& color, const QImage& text, const int k, const bool findBlack) {
  QImage dst = text.convertToFormat(QImage::Format_RGB32);
  const BinaryImage content(text);
  const BinaryImage wholePage(text.size(), imageproc::BLACK);
  imageproc::stexp::hsvKMeansInPlace(dst, color.convertToFormat(QImage::Format_RGB32), content, wholePage, k, 128, 0,
                                     0.0f, 0.0f, 0.0f, findBlack, false);
  return dst;
}
}  // namespace

QImage textOf(const QImage& color) {
  const imageproc::GrayImage gray(color);
  return BinaryImage(gray.toQImage(), imageproc::BinaryThreshold::otsuThreshold(gray.toQImage())).toQImage();
}

const std::vector<ColorMethod>& colorMethods() {
  static const std::vector<ColorMethod> list = {
      {"segmented", "Segmentation without reduction", "ours", "each letter in its average color",
       [](const QImage& c, const QImage& t) { return segmented(c, t); }},
      {"posterize4", "Posterize, 4 levels", "ours", "default in the Output step, black and white forced",
       [](const QImage& c, const QImage& t) { return posterized(c, t, 4); }},
      {"posterize6", "Posterize, 6 levels", "ours", "black and white forced",
       [](const QImage& c, const QImage& t) { return posterized(c, t, 6); }},
      {"kmeans4", "K-Means, 4 colors", "experimental", "free",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 4, false); }},
      {"kmeans4black", "K-Means, 4 colors, black fixed", "experimental", "one cluster is pure black",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 4, true); }},
      {"kmeans6black", "K-Means, 6 colors, black fixed", "experimental", "one cluster is pure black",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 6, true); }},
      {"kmeans6pixel", "K-Means per pixel, 6 colors, black fixed", "experimental", "each pixel on its own",
       [](const QImage& c, const QImage& t) { return textKMeans(c, t, 6, false); }},
      {"kmeans6combined", "K-Means combined, 6 colors, black fixed", "experimental",
       "letters in one color, large areas per pixel",
       [](const QImage& c, const QImage& t) { return textKMeans(c, t, 6, true); }},
      {"expkmeans6", "Experimental, 6 colors", "filter", "original code, defaults",
       [](const QImage& c, const QImage& t) { return experimentalKMeans(c, t, 6, false); }},
      {"expkmeans6black", "Experimental, 6 colors, find black", "filter", "original code, defaults, \"find black\"",
       [](const QImage& c, const QImage& t) { return experimentalKMeans(c, t, 6, true); }},
  };
  return list;
}
}  // namespace binarization_compare
