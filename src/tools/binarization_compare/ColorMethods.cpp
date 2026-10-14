// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorMethods.h"

#include <cmath>

#include "BinaryImage.h"
#include "BinaryThreshold.h"
#include "ColorKMeans.h"
#include "ColorSegmenter.h"
#include "Dpi.h"
#include "GrayImage.h"
#include "Posterizer.h"

namespace binarization_compare {
namespace {
using imageproc::BinaryImage;

// The noise reduction of the color segmentation, as the default of the Output step.
const int kNoiseReduction = 7;

Dpi dpiOf(const QImage& image) {
  const int x = static_cast<int>(std::lround(image.dotsPerMeterX() * 0.0254));
  const int y = static_cast<int>(std::lround(image.dotsPerMeterY() * 0.0254));
  return Dpi(x > 0 ? x : 300, y > 0 ? y : 300);
}

// Each letter in the average color of its pixels, the background white.
QImage segmented(const QImage& color, const QImage& text) {
  const imageproc::ColorSegmenter segmenter(dpiOf(color), kNoiseReduction);
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
}  // namespace

QImage textOf(const QImage& color) {
  const imageproc::GrayImage gray(color);
  return BinaryImage(gray.toQImage(), imageproc::BinaryThreshold::otsuThreshold(gray.toQImage())).toQImage();
}

const std::vector<ColorMethod>& colorMethods() {
  static const std::vector<ColorMethod> list = {
      {"segmented", "Segmentierung ohne Reduktion", "ours", "jeder Buchstabe in seiner Durchschnittsfarbe",
       [](const QImage& c, const QImage& t) { return segmented(c, t); }},
      {"posterize4", "Posterisieren, 4 Stufen", "ours", "Standard in Schritt 6, Schwarz und Weiß erzwungen",
       [](const QImage& c, const QImage& t) { return posterized(c, t, 4); }},
      {"posterize6", "Posterisieren, 6 Stufen", "ours", "Schwarz und Weiß erzwungen",
       [](const QImage& c, const QImage& t) { return posterized(c, t, 6); }},
      {"kmeans4", "K-Means, 4 Farben", "experimental", "frei",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 4, false); }},
      {"kmeans4black", "K-Means, 4 Farben, Schwarz fest", "experimental", "eine Gruppe ist reines Schwarz",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 4, true); }},
      {"kmeans6black", "K-Means, 6 Farben, Schwarz fest", "experimental", "eine Gruppe ist reines Schwarz",
       [](const QImage& c, const QImage& t) { return kmeans(c, t, 6, true); }},
  };
  return list;
}
}  // namespace binarization_compare
